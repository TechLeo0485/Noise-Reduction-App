#pragma once

#include "ai/voxlock/VoxLockRuntime.h"
#include "ai/voxlock/VoxLockStream.h"
#include "audio/buffers/Resampler.h"
#include "core/SpscRingBuffer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vs {

enum class IsolationState : int {
    NotBuilt = 0,        // built without VOICESTUDIO_WITH_VOXLOCK
    Uninitialized = 1,   // initialize() not called yet
    Loading = 2,         // loading ONNX Runtime / models / the saved voice
    NeedsEnrollment = 3, // ready, but no voice has been recorded
    Ready = 4,           // a voice is enrolled and the model is running
    Error = 5,           // runtime or models unusable; see message
};

[[nodiscard]] const char* isolationStateName(IsolationState state) noexcept;

struct IsolationStatus {
    IsolationState state = IsolationState::Uninitialized;
    std::string message;         // user-facing
    bool busy = false;           // an enrollment is being prepared (the previous voice keeps running)
    bool enrolled = false;       // a voice sample is saved on this computer
    std::string backend;         // e.g. "ONNX Runtime 1.31.0 (CPU), VoxLock INT8 Full"
    int framesPerRun = 0;        // chosen chunk size K (8 ms hops per inference)
    int threads = 0;
    double calibrationP50Ms = 0.0;
    double calibrationP95Ms = 0.0;
    std::string enrolledMicrophone; // microphone the voice sample was recorded with (empty if unknown)
    int adaptations = 0;            // times the chunk size was raised at runtime because the CPU fell behind
};

struct IsolationFrameInfo {
    bool enabled = false;
    bool delivering = false;     // this frame carries isolated audio (false: silence or isolation off)
    int latencySamples = 0;      // delay of the isolated audio behind the microphone at 48 kHz
};

// Strict Voice Isolation: keeps only the enrolled voice (VoxLock target-speaker extraction).
//
//   capture thread  : process() -> input ring -> [worker] -> output ring -> process()
//   worker thread   : 48 kHz -> 16 kHz -> voxlock::Stream (ONNX Runtime) -> 48 kHz
//   loader thread   : ONNX Runtime + models, enrollment (enroll.onnx), per-voice sessions, calibration
//
// Fail-closed: while enabled, process() outputs the isolated voice or silence - never the microphone
// signal. That covers loading, a missing/invalid model, a missing enrollment, a worker that falls behind
// and inference errors. The capture thread never waits for the worker; it only touches lock-free rings
// and atomics.
class VoiceIsolator {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr std::size_t kFrameSize = 480;
    static constexpr std::size_t kRecordMaxSamples = static_cast<std::size_t>(kSampleRate) * 20;

    using ModelFactory = std::function<std::unique_ptr<voxlock::IChunkModel>(int framesPerRun)>;

    VoiceIsolator();
    ~VoiceIsolator();
    VoiceIsolator(const VoiceIsolator&) = delete;
    VoiceIsolator& operator=(const VoiceIsolator&) = delete;

    // ---- Control (non-real-time, any thread) -----------------------------------------------------
    // Loads onnxruntime.dll from runtimeDir, the models from modelDir and, if it exists, the voice saved
    // in enrollmentFile. Asynchronous; follow progress with status().
    void initialize(const std::filesystem::path& runtimeDir, const std::filesystem::path& modelDir,
                    const std::filesystem::path& enrollmentFile);
    // Tests and tools: use `factory` instead of ONNX Runtime; no enrollment needed (state becomes Ready).
    void initializeWithModel(ModelFactory factory, int framesPerRun = 2);

    // Validates and saves a 16 kHz recording as the enrolled voice, then switches the model to it.
    // `microphone` (the device it was recorded with) is saved alongside, so the UI can warn when a
    // different microphone is in use. Asynchronous. On a validation error the previous voice stays
    // active and status().message explains.
    void enroll(std::vector<float> audio16k, std::string microphone = {});
    // Deletes the saved voice. While isolation is enabled the output is silent until a new voice is enrolled.
    void deleteEnrollment();
    // Blocks until queued control work (initialize/enroll/delete) has finished. For tests and tools.
    void waitIdle();

    [[nodiscard]] IsolationStatus status() const;
    [[nodiscard]] IsolationState state() const noexcept { return static_cast<IsolationState>(state_.load(std::memory_order_acquire)); }
    [[nodiscard]] std::uint64_t statusRevision() const noexcept { return statusRevision_.load(std::memory_order_acquire); }

    // ---- Voice enrollment recording (UI starts it, the capture thread fills it) ----------------------
    enum class RecordingState : int { Idle = 0, Recording = 1, Ready = 2 };
    bool startRecording(double seconds) noexcept;
    void cancelRecording() noexcept;
    [[nodiscard]] RecordingState recordingState() const noexcept {
        return static_cast<RecordingState>(recordState_.load(std::memory_order_acquire));
    }
    [[nodiscard]] float recordingProgress() const noexcept;
    [[nodiscard]] float recordingPeak() const noexcept { return recordPeak_.load(std::memory_order_relaxed); }
    // The finished recording resampled to 16 kHz (valid in the Ready state); returns to Idle.
    [[nodiscard]] std::vector<float> takeRecording16k();

    // ---- Real-time (capture thread) -----------------------------------------------------------------
    // Call only while process() is not running (engine stopped).
    void reset() noexcept;
    // Processes one kFrameSize frame in place. enabled=false leaves the frame untouched.
    void process(float* frame, bool enabled, IsolationFrameInfo& info) noexcept;

    // ---- Statistics (any thread) ------------------------------------------------------------------
    struct Metrics {
        std::atomic<std::uint64_t> underruns{0};      // worker late: silence was output
        std::atomic<std::uint64_t> overruns{0};       // input ring full: stream restarted
        std::atomic<std::uint64_t> modelFailures{0};  // inference errors (each mutes until restart)
        std::atomic<std::uint64_t> restarts{0};
        std::atomic<std::uint64_t> silencedFrames{0}; // enabled, but no isolated audio was available
        std::atomic<std::uint64_t> isolatedFrames{0};
        std::atomic<float> latencyMs{0.0f};           // measured delay of the isolated voice (48 kHz in/out)
        std::atomic<float> lastInferenceUs{0.0f};
        std::atomic<bool> delivering{false};
        SpscRingBuffer<float> inferenceUs{1024};     // per chunk.onnx call, drained by the UI
    };
    [[nodiscard]] Metrics& metrics() noexcept { return metrics_; }

private:
    struct Prepared;
    class TimedModel;

    void startThreads();
    void post(std::function<void()> task);
    void loaderMain();
    void workerMain();
    void setStatus(IsolationState state, const std::string& message, bool busy);
    void monitor();
    [[nodiscard]] std::filesystem::path microphoneFile() const;
    bool prepareVoice(const std::vector<float>& audio16k, std::string& error);
    void publish(std::unique_ptr<voxlock::IChunkModel> model, int framesPerRun);
    void requestRestart() noexcept;
    void workerReset();
    void workerProcess();

    // Control / status
    mutable std::mutex statusMutex_;
    IsolationStatus status_{};
    std::atomic<int> state_{static_cast<int>(IsolationState::Uninitialized)};
    std::atomic<std::uint64_t> statusRevision_{0};
    std::filesystem::path runtimeDir_;
    std::filesystem::path modelDir_;
    std::filesystem::path enrollmentFile_;
    std::unique_ptr<voxlock::Runtime> runtime_; // loader thread only
    ModelFactory testFactory_;
    voxlock::EnrollmentTensors tensors_;        // loader thread only: current voice, for rebuilding sessions
    bool haveTensors_ = false;

    // Health monitor (loader thread, once per second while idle): underruns and inference failures over time.
    struct HealthSample {
        std::chrono::steady_clock::time_point time;
        std::uint64_t underruns;
        std::uint64_t failures;
    };
    std::deque<HealthSample> health_;

    // Loader thread
    std::mutex loaderMutex_;
    std::condition_variable loaderWake_;
    std::condition_variable loaderIdle_;
    std::deque<std::function<void()>> loaderTasks_;
    bool loaderBusy_ = false;
    bool quit_ = false;
    std::thread loader_;

    // Model hand-over: loader -> worker (taken at the next restart)
    std::mutex modelMutex_;
    std::unique_ptr<voxlock::IChunkModel> pendingModel_;
    int pendingFramesPerRun_ = 0;
    bool pendingClear_ = false;
    std::atomic<std::uint32_t> modelGeneration_{0};
    std::atomic<bool> modelReady_{false}; // a model is (or is about to be) running

    // Worker thread
    std::thread worker_;
    void* wakeEvent_ = nullptr;
    std::atomic<bool> workerQuit_{false};
    std::atomic<std::uint32_t> requestedEpoch_{0};
    std::atomic<std::uint32_t> ackedEpoch_{0};
    std::atomic<bool> workerFailed_{false};
    std::atomic<int> activeFramesPerRun_{0};
    std::unique_ptr<TimedModel> model_;
    std::unique_ptr<voxlock::Stream> stream_;
    Resampler down_;
    Resampler up_;
    std::vector<float> in48_;
    std::vector<float> buf16_;
    std::vector<float> out16_;
    std::vector<float> out48_;

    SpscRingBuffer<float> inRing_;
    SpscRingBuffer<float> outRing_;

    // Capture-thread state
    bool wasEnabled_ = false;
    bool restartPending_ = false;
    bool priming_ = true;
    std::uint32_t epoch_ = 0;
    std::uint32_t seenGeneration_ = 0;
    std::size_t startThreshold_ = 0;
    std::size_t marginSamples_ = 0;
    long long pushed_ = 0;
    long long popped_ = 0;
    std::size_t fadeIn_ = 0;     // samples of the current fade-in already applied
    bool lastFrameDry_ = false;  // the previous frame passed the microphone through (isolation off)
    int lastLatencySamples_ = 0;
    // Latency trimming: the start-up cushion assumes the worst phase between 10 ms frames and 8 ms
    // hops; once running, slack above the safety margin is dropped during a quiet moment.
    std::size_t minFill_ = SIZE_MAX; // smallest output-ring fill seen before a read in this window
    int fillWindowFrames_ = 0;
    std::size_t pendingTrim_ = 0;
    int quietFrames_ = 0;
    std::array<float, 2 * kFrameSize> trimScratch_{};
    std::array<float, kFrameSize> scratch_{};

    // Enrollment recording
    std::vector<float> recordBuffer_;
    std::atomic<int> recordState_{0};
    std::atomic<std::size_t> recordLength_{0};
    std::atomic<std::size_t> recordPosition_{0};
    std::atomic<float> recordPeak_{0.0f};

    Metrics metrics_;
};

} // namespace vs
