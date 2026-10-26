#pragma once

#include "audio/EngineConfig.h"
#include "audio/VoiceProcessingChain.h"
#include "audio/buffers/AdaptiveRingReader.h"
#include "audio/buffers/Resampler.h"
#include "audio/monitoring/EngineMetrics.h"
#include "audio/wasapi/WasapiCaptureStream.h"
#include "audio/wasapi/WasapiRenderStream.h"
#include "core/Parameters.h"
#include "core/SpscRingBuffer.h"

#include <array>
#include <string>
#include <vector>

namespace vs {

struct EngineRuntimeInfo {
    bool captureRunning = false;
    bool outputRunning = false;
    bool monitorRunning = false;
    StreamInfo capture{};
    StreamInfo output{};
    StreamInfo monitor{};
    std::wstring inputDeviceId;
    std::wstring outputDeviceId;
    std::wstring monitorDeviceId;
    std::string inputName;
    std::string outputName;
    std::string monitorName;
    std::string outputError;
    std::string monitorError;
    bool captureResampling = false;
    double captureResamplerMs = 0.0;
    double outputResamplerMs = 0.0;
    double outputTargetMs = 0.0;  // effective jitter-buffer target
    double monitorTargetMs = 0.0;
    double fixedLatencyMs = 0.0;    // worst case of everything except strict isolation, incl. the virtual mic
    double isolationMaxMs = 0.0;    // what the 300 ms budget leaves for strict isolation
};

// Owns the real-time pipeline:
//
//   WASAPI capture thread:  device packets -> mono float -> (resample to 48 kHz)
//                           -> 10 ms framing -> VoiceProcessingChain -> SPSC rings
//   WASAPI render threads:  SPSC ring -> adaptive resampler (drift) -> device format
//
// start()/stop() are non-real-time and must be called from one control thread
// (EngineSupervisor). Everything reachable from the stream threads is preallocated.
class AudioEngine final : private ICaptureSink {
public:
    struct StartResult {
        bool ok = false;
        HRESULT hr = S_OK;
        std::string error;
    };

    explicit AudioEngine(EngineParameters& params);
    ~AudioEngine() override;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    StartResult start(const EngineConfig& config, IMMDeviceEnumerator* enumerator);
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] StreamFailure& failure() noexcept { return failure_; }
    [[nodiscard]] EngineMetrics& metrics() noexcept { return metrics_; }
    [[nodiscard]] const EngineMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] const EngineRuntimeInfo& runtimeInfo() const noexcept { return info_; }
    [[nodiscard]] bool isSuppressorAvailable(SuppressorKind kind) const noexcept { return chain_.isAvailable(kind); }
    // Lives as long as the engine (independent of stream restarts).
    [[nodiscard]] VoiceIsolator& isolator() noexcept { return chain_.isolator(); }

    // ---- Voice check: record a few seconds of the processed output for playback --------------
    // Only on explicit user action. Audio stays in this preallocated buffer (never written to disk).
    enum class VoiceCheckState : int { Idle = 0, Recording = 1, Ready = 2 };
    static constexpr std::size_t kVoiceCheckMaxSamples = 48000 * 10;

    // UI thread. Returns false if a recording is already in progress.
    bool startVoiceCheck(double seconds) noexcept;
    void cancelVoiceCheck() noexcept;
    [[nodiscard]] VoiceCheckState voiceCheckState() const noexcept {
        return static_cast<VoiceCheckState>(voiceCheckState_.load(std::memory_order_acquire));
    }
    [[nodiscard]] float voiceCheckProgress() const noexcept;
    // Copies the finished recording (valid only in the Ready state) and returns to Idle.
    [[nodiscard]] std::vector<float> takeVoiceCheck();

private:
    class RenderAdapter final : public IRenderSource {
    public:
        SpscRingBuffer<float>* ring = nullptr;
        RenderPathMetrics* metrics = nullptr;
        AdaptiveRingReader reader;
        void renderMono(float* out, std::size_t frames) noexcept override { reader.render(*ring, out, frames, *metrics); }
    };

    void onCapturedAudio(const float* mono, std::size_t frames, bool discontinuity) noexcept override;
    void consumeInternalRate(const float* samples, std::size_t count) noexcept;
    void processFrame() noexcept;

    bool openRender(IMMDeviceEnumerator* enumerator, const std::wstring& id, StreamRole role, const EngineConfig& config,
                    WasapiRenderStream& stream, RenderAdapter& adapter, SpscRingBuffer<float>& ring,
                    RenderPathMetrics& metrics, std::wstring& resolvedId, std::string& name, std::string& error,
                    double& effectiveTargetMs);

    EngineParameters& params_;
    VoiceProcessingChain chain_;
    EngineMetrics metrics_;
    StreamFailure failure_;

    SpscRingBuffer<float> outputRing_;
    SpscRingBuffer<float> monitorRing_;

    WasapiCaptureStream capture_;
    WasapiRenderStream output_;
    WasapiRenderStream monitor_;
    RenderAdapter outputAdapter_;
    RenderAdapter monitorAdapter_;

    Resampler captureResampler_;
    bool captureNeedsResample_ = false;
    std::vector<float> resampled_;

    std::array<float, VoiceProcessingChain::kFrameSize> frame_{};
    std::array<float, VoiceProcessingChain::kFrameSize> monitorScratch_{};
    std::size_t frameFill_ = 0;

    // Configuration snapshot read by the capture thread (written only while stopped).
    bool outputActive_ = false;
    bool monitorActive_ = false;
    double deadlineUs_ = 5000.0;
    float inputPeakHold_ = 0.0f;
    float outputPeakHold_ = 0.0f;

    bool running_ = false;
    EngineRuntimeInfo info_{};

    // Voice check: written by the capture thread while Recording, read by the UI when Ready.
    std::vector<float> voiceCheckBuffer_;
    std::atomic<int> voiceCheckState_{0};
    std::atomic<std::size_t> voiceCheckLength_{0};
    std::atomic<std::size_t> voiceCheckPosition_{0};
};

} // namespace vs
