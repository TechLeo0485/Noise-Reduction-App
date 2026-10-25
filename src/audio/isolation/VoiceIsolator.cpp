#include "audio/isolation/VoiceIsolator.h"

#include "ai/voxlock/VoxLockEnrollment.h"
#include "core/RealtimeThread.h"
#include "diagnostics/Logger.h"

#if defined(VOICESTUDIO_HAS_VOXLOCK)
#include "ai/voxlock/VoxLockRuntime.h"
#endif

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace vs {

namespace {

constexpr std::size_t kFrameSizeConst = VoiceIsolator::kFrameSize;
constexpr std::size_t kRingCapacity = 48000;   // 1 s at 48 kHz in each direction
constexpr std::size_t kWorkerBlock = 960;      // 48 kHz samples taken from the input ring at a time
constexpr int kMaxPush16 = 1024;               // 16 kHz samples per Stream::push
constexpr std::size_t kMaxStreamOutput = 4096; // >= Stream::maxOutputSamples() for K <= 8
constexpr int kDownHalfTaps = 48;              // 48 -> 16 kHz anti-alias filter, 1 ms look-ahead
constexpr int kUpHalfTaps = 16;                // 16 -> 48 kHz interpolation, 1 ms look-ahead
constexpr std::size_t kInitialMargin = 960;    // 20 ms safety fill: covers inference spikes (measured max ~18 ms)
constexpr std::size_t kMarginStep = 240;       // added after every underrun ...
constexpr std::size_t kMaxMargin = 2880;       // ... up to 60 ms
constexpr std::size_t kDryFadeSamples = 96;    // 2 ms ramp-down of the microphone when isolation turns on
constexpr int kTrimWindowFrames = 200;         // 2 s of fill observations per latency-trim decision
constexpr std::size_t kMinTrim = 96;           // ignore slack below 2 ms
constexpr std::size_t kMaxTrim = kFrameSizeConst / 2; // at most 5 ms per step, so a clipped onset is inaudible
constexpr float kQuietPeak = 0.003f;           // about -50 dBFS: a trim here is inaudible
constexpr int kThreads = 2;                    // intra-op threads per inference (measured: 4 is no faster)
constexpr double kHopMs = 1000.0 * voxlock::kHop / voxlock::kSampleRate;

std::size_t burstSamples48k(int framesPerRun) {
    // One inference releases K hops of 16 kHz audio, i.e. 3 * K * 128 samples at 48 kHz.
    return static_cast<std::size_t>(framesPerRun) * voxlock::kHop * 3;
}

} // namespace

const char* isolationStateName(IsolationState state) noexcept {
    switch (state) {
    case IsolationState::NotBuilt: return "notBuilt";
    case IsolationState::Uninitialized: return "uninitialized";
    case IsolationState::Loading: return "loading";
    case IsolationState::NeedsEnrollment: return "needsEnrollment";
    case IsolationState::Ready: return "ready";
    case IsolationState::Error: return "error";
    }
    return "unknown";
}

// Forwards to the real model and records the inference time of every call.
class VoiceIsolator::TimedModel final : public voxlock::IChunkModel {
public:
    TimedModel(std::unique_ptr<voxlock::IChunkModel> inner, Metrics& metrics) : inner_(std::move(inner)), metrics_(metrics) {}

    [[nodiscard]] int framesPerRun() const noexcept override { return inner_->framesPerRun(); }

    bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut, float* cOut) noexcept override {
        const std::int64_t start = HighResClock::now();
        const bool ok = inner_->run(wav, hIn, cIn, frames, hOut, cOut);
        const auto us = static_cast<float>(HighResClock::ticksToMicroseconds(HighResClock::now() - start));
        metrics_.lastInferenceUs.store(us, std::memory_order_relaxed);
        metrics_.inferenceUs.tryPush(&us, 1);
        return ok;
    }

private:
    std::unique_ptr<voxlock::IChunkModel> inner_;
    Metrics& metrics_;
};

VoiceIsolator::VoiceIsolator()
    : in48_(kWorkerBlock), buf16_(kMaxPush16), out16_(kMaxStreamOutput), out48_(kMaxStreamOutput * 3 + 64),
      inRing_(kRingCapacity), outRing_(kRingCapacity), recordBuffer_(kRecordMaxSamples, 0.0f) {
    down_.configure(kSampleRate, voxlock::kSampleRate, kWorkerBlock, kDownHalfTaps);
    up_.configure(voxlock::kSampleRate, kSampleRate, kMaxStreamOutput, kUpHalfTaps);
#if !defined(VOICESTUDIO_HAS_VOXLOCK)
    status_.state = IsolationState::NotBuilt;
    status_.message = "Strict voice isolation is not included in this build.";
    state_.store(static_cast<int>(IsolationState::NotBuilt), std::memory_order_release);
#endif
}

VoiceIsolator::~VoiceIsolator() {
    {
        std::lock_guard lock(loaderMutex_);
        quit_ = true;
        loaderTasks_.clear();
    }
    loaderWake_.notify_all();
    if (loader_.joinable()) {
        loader_.join();
    }
    workerQuit_.store(true, std::memory_order_release);
    if (wakeEvent_ != nullptr) {
        SetEvent(wakeEvent_);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (wakeEvent_ != nullptr) {
        CloseHandle(wakeEvent_);
    }
}

// ---- Control --------------------------------------------------------------------------------------

void VoiceIsolator::startThreads() {
    if (worker_.joinable()) {
        return;
    }
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    worker_ = std::thread([this] { workerMain(); });
    loader_ = std::thread([this] { loaderMain(); });
}

void VoiceIsolator::post(std::function<void()> task) {
    {
        std::lock_guard lock(loaderMutex_);
        loaderTasks_.push_back(std::move(task));
    }
    loaderWake_.notify_all();
}

void VoiceIsolator::waitIdle() {
    std::unique_lock lock(loaderMutex_);
    loaderIdle_.wait(lock, [this] { return quit_ || (loaderTasks_.empty() && !loaderBusy_); });
}

void VoiceIsolator::loaderMain() {
    setCurrentThreadName(L"VoiceIsolator loader");
    preferPerformanceCores(); // calibration must measure the cores the worker will run on
    std::unique_lock lock(loaderMutex_);
    for (;;) {
        loaderWake_.wait(lock, [this] { return quit_ || !loaderTasks_.empty(); });
        if (quit_) {
            break;
        }
        std::function<void()> task = std::move(loaderTasks_.front());
        loaderTasks_.pop_front();
        loaderBusy_ = true;
        lock.unlock();
        try {
            task();
        } catch (const std::exception& e) {
            logError(std::string("Voice isolation: ") + e.what());
            setStatus(IsolationState::Error, std::string("Voice isolation failed: ") + e.what(), false);
        }
        lock.lock();
        loaderBusy_ = false;
        loaderIdle_.notify_all();
    }
    loaderIdle_.notify_all();
#if defined(VOICESTUDIO_HAS_VOXLOCK)
    runtime_.reset();
#endif
}

void VoiceIsolator::setStatus(IsolationState state, const std::string& message, bool busy) {
    {
        std::lock_guard lock(statusMutex_);
        status_.state = state;
        status_.message = message;
        status_.busy = busy;
    }
    state_.store(static_cast<int>(state), std::memory_order_release);
    statusRevision_.fetch_add(1, std::memory_order_acq_rel);
}

IsolationStatus VoiceIsolator::status() const {
    std::lock_guard lock(statusMutex_);
    return status_;
}

void VoiceIsolator::initialize(const std::filesystem::path& runtimeDir, const std::filesystem::path& modelDir,
                               const std::filesystem::path& enrollmentFile) {
#if !defined(VOICESTUDIO_HAS_VOXLOCK)
    (void)runtimeDir;
    (void)modelDir;
    (void)enrollmentFile;
#else
    if (worker_.joinable()) {
        return; // already initialised
    }
    runtimeDir_ = runtimeDir;
    modelDir_ = modelDir;
    enrollmentFile_ = enrollmentFile;
    setStatus(IsolationState::Loading, "Loading the voice isolation model...", false);
    startThreads();
    post([this] {
        std::string error;
        runtime_ = voxlock::Runtime::create(runtimeDir_, modelDir_, error);
        if (!runtime_) {
            logError("Voice isolation unavailable: " + error);
            setStatus(IsolationState::Error, "Voice isolation is unavailable: " + error, false);
            return;
        }
        {
            std::lock_guard lock(statusMutex_);
            status_.backend = runtime_->description();
        }
        logInfo("Voice isolation: " + runtime_->description());
        std::error_code ec;
        if (!enrollmentFile_.empty() && std::filesystem::exists(enrollmentFile_, ec)) {
            std::vector<float> voice;
            if (voxlock::loadVoiceSample(enrollmentFile_, voice, error) && prepareVoice(voice, error)) {
                std::lock_guard lock(statusMutex_);
                status_.enrolled = true;
                return;
            }
            logWarning("Saved voice sample could not be used: " + error);
            setStatus(IsolationState::NeedsEnrollment, "Your saved voice could not be loaded (" + error + "). Record it again.",
                      false);
            return;
        }
        setStatus(IsolationState::NeedsEnrollment, "Record your voice once to use strict isolation.", false);
    });
#endif
}

void VoiceIsolator::initializeWithModel(ModelFactory factory, int framesPerRun) {
    testFactory_ = std::move(factory);
    startThreads();
    auto model = testFactory_(framesPerRun);
    publish(std::move(model), framesPerRun);
    {
        std::lock_guard lock(statusMutex_);
        status_.enrolled = true;
        status_.framesPerRun = framesPerRun;
        status_.backend = "test model";
    }
    setStatus(IsolationState::Ready, "Isolating your voice.", false);
}

void VoiceIsolator::enroll(std::vector<float> audio16k) {
    if (state() == IsolationState::NotBuilt) {
        return;
    }
    post([this, audio = std::move(audio16k)] {
        const IsolationState before = state();
        std::string error;
        if (!voxlock::validateVoiceSample(audio, error)) {
            setStatus(before, error, false);
            return;
        }
        if (!testFactory_ && before != IsolationState::Ready && before != IsolationState::NeedsEnrollment) {
            setStatus(before, "The voice isolation model is not loaded.", false);
            return;
        }
        setStatus(before, "Preparing your voice...", true);
        if (!prepareVoice(audio, error)) {
            logError("Voice enrollment failed: " + error);
            setStatus(before, "Voice enrollment failed: " + error, false);
            return;
        }
        if (!enrollmentFile_.empty() && !voxlock::saveVoiceSample(enrollmentFile_, audio, error)) {
            logError("Saving the voice sample failed: " + error);
            setStatus(IsolationState::Ready, "Isolating your voice, but it could not be saved for next time: " + error, false);
            return;
        }
        std::lock_guard lock(statusMutex_);
        status_.enrolled = true;
    });
}

void VoiceIsolator::deleteEnrollment() {
    if (state() == IsolationState::NotBuilt) {
        return;
    }
    post([this] {
        std::error_code ec;
        if (!enrollmentFile_.empty()) {
            std::filesystem::remove(enrollmentFile_, ec);
        }
        {
            std::lock_guard lock(modelMutex_);
            pendingModel_.reset();
            pendingClear_ = true;
        }
        modelReady_.store(false, std::memory_order_release);
        modelGeneration_.fetch_add(1, std::memory_order_acq_rel);
        if (wakeEvent_ != nullptr) {
            SetEvent(wakeEvent_);
        }
        {
            std::lock_guard lock(statusMutex_);
            status_.enrolled = false;
            status_.framesPerRun = 0;
        }
        logInfo("Voice sample deleted");
        const IsolationState next = state() == IsolationState::Error ? IsolationState::Error : IsolationState::NeedsEnrollment;
        setStatus(next, ec ? "The voice sample could not be deleted: " + ec.message()
                           : "Your voice sample was deleted. Record it again to use strict isolation.",
                  false);
    });
}

void VoiceIsolator::publish(std::unique_ptr<voxlock::IChunkModel> model, int framesPerRun) {
    {
        std::lock_guard lock(modelMutex_);
        pendingModel_ = std::move(model);
        pendingFramesPerRun_ = framesPerRun;
        pendingClear_ = false;
    }
    modelReady_.store(true, std::memory_order_release);
    modelGeneration_.fetch_add(1, std::memory_order_acq_rel); // the capture thread restarts the stream with it
    if (wakeEvent_ != nullptr) {
        SetEvent(wakeEvent_);
    }
}

bool VoiceIsolator::prepareVoice(const std::vector<float>& audio16k, std::string& error) {
    if (testFactory_) {
        const int k = std::max(1, status().framesPerRun);
        auto model = testFactory_(k);
        if (!model) {
            error = "test model unavailable";
            return false;
        }
        publish(std::move(model), k);
        setStatus(IsolationState::Ready, "Isolating your voice.", false);
        return true;
    }
#if defined(VOICESTUDIO_HAS_VOXLOCK)
    std::vector<float> sample;
    if (!voxlock::fitSample(audio16k, sample, error)) {
        return false;
    }
    voxlock::EnrollmentTensors tensors;
    if (!runtime_->enroll(sample, tensors, error)) {
        return false;
    }

    // Calibrate the chunk size K on this machine: the smallest K whose measured inference time leaves a
    // safe margin. Each extra hop adds 8 ms of latency, so K is kept as small as the CPU allows.
    std::unique_ptr<voxlock::IChunkModel> chosen;
    int chosenK = 0;
    double p50 = 0.0;
    double p95 = 0.0;
    std::mt19937 rng(1234);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    for (int k : {2, 3, 4, 5}) {
        voxlock::ChunkModelOptions options;
        options.framesPerRun = k;
        options.threads = kThreads;
        auto model = runtime_->createChunkModel(tensors, options, error);
        if (!model) {
            return false;
        }
        std::vector<float> wav(static_cast<std::size_t>(voxlock::chunkInputSamples(k)));
        for (float& v : wav) {
            v = noise(rng);
        }
        std::vector<float> frames(static_cast<std::size_t>(k) * voxlock::kNfft);
        std::vector<float> h[2] = {std::vector<float>(voxlock::kStateSize), std::vector<float>(voxlock::kStateSize)};
        std::vector<float> c[2] = {std::vector<float>(voxlock::kStateSize), std::vector<float>(voxlock::kStateSize)};
        std::vector<double> times;
        int s = 0;
        for (int i = 0; i < 46; ++i) {
            const std::int64_t t0 = HighResClock::now();
            if (!model->run(wav.data(), h[s].data(), c[s].data(), frames.data(), h[s ^ 1].data(), c[s ^ 1].data())) {
                error = "inference failed during calibration";
                return false;
            }
            s ^= 1;
            if (i >= 6) {
                times.push_back(HighResClock::ticksToMilliseconds(HighResClock::now() - t0));
            }
        }
        std::sort(times.begin(), times.end());
        p50 = times[times.size() / 2];
        p95 = times[times.size() * 95 / 100];
        const double budget = k * kHopMs;
        logInfo("Voice isolation calibration K=" + std::to_string(k) + ": p50 " + std::to_string(p50) + " ms, p95 " +
                std::to_string(p95) + " ms per " + std::to_string(budget) + " ms of audio");
        chosen = std::move(model);
        chosenK = k;
        if (p50 <= 0.75 * budget && p95 <= budget) {
            break;
        }
    }
    publish(std::move(chosen), chosenK);
    {
        std::lock_guard lock(statusMutex_);
        status_.framesPerRun = chosenK;
        status_.threads = kThreads;
        status_.calibrationP50Ms = p50;
        status_.calibrationP95Ms = p95;
    }
    const double budget = chosenK * kHopMs;
    if (p50 > 0.75 * budget || p95 > budget) {
        setStatus(IsolationState::Ready,
                  "This computer is barely fast enough for real-time isolation; expect short silent gaps.", false);
    } else {
        setStatus(IsolationState::Ready, "Isolating your voice.", false);
    }
    return true;
#else
    (void)audio16k;
    error = "not included in this build";
    return false;
#endif
}

// ---- Enrollment recording ------------------------------------------------------------------------------

bool VoiceIsolator::startRecording(double seconds) noexcept {
    if (recordingState() == RecordingState::Recording) {
        return false;
    }
    const auto length = std::clamp<std::size_t>(static_cast<std::size_t>(seconds * kSampleRate),
                                                static_cast<std::size_t>(kSampleRate) * 3, kRecordMaxSamples);
    recordLength_.store(length, std::memory_order_relaxed);
    recordPosition_.store(0, std::memory_order_relaxed);
    recordPeak_.store(0.0f, std::memory_order_relaxed);
    recordState_.store(static_cast<int>(RecordingState::Recording), std::memory_order_release);
    return true;
}

void VoiceIsolator::cancelRecording() noexcept {
    recordState_.store(static_cast<int>(RecordingState::Idle), std::memory_order_release);
}

float VoiceIsolator::recordingProgress() const noexcept {
    const std::size_t length = recordLength_.load(std::memory_order_relaxed);
    return length > 0 ? static_cast<float>(recordPosition_.load(std::memory_order_relaxed)) / static_cast<float>(length) : 0.0f;
}

std::vector<float> VoiceIsolator::takeRecording16k() {
    std::vector<float> out;
    if (recordingState() != RecordingState::Ready) { // acquire: the capture thread's writes are visible
        return out;
    }
    const std::size_t n = recordPosition_.load(std::memory_order_relaxed);
    Resampler resampler;
    resampler.configure(kSampleRate, voxlock::kSampleRate, n + kDownHalfTaps + 16, kDownHalfTaps);
    resampler.write(recordBuffer_.data(), n);
    const std::vector<float> tail(kDownHalfTaps + 4, 0.0f); // flush the filter look-ahead
    resampler.write(tail.data(), tail.size());
    out.resize(n / 3 + 8);
    out.resize(std::min(resampler.read(out.data(), out.size()), n / 3));
    recordState_.store(static_cast<int>(RecordingState::Idle), std::memory_order_release);
    return out;
}

// ---- Real-time: capture thread -------------------------------------------------------------------

void VoiceIsolator::reset() noexcept {
    wasEnabled_ = false;
    lastFrameDry_ = false;
    restartPending_ = false;
    priming_ = true;
    fadeIn_ = 0;
    metrics_.delivering.store(false, std::memory_order_relaxed);
}

void VoiceIsolator::requestRestart() noexcept {
    ++epoch_;
    requestedEpoch_.store(epoch_, std::memory_order_release);
    restartPending_ = true;
    metrics_.restarts.fetch_add(1, std::memory_order_relaxed);
    if (wakeEvent_ != nullptr) {
        SetEvent(wakeEvent_);
    }
}

void VoiceIsolator::process(float* frame, bool enabled, IsolationFrameInfo& info) noexcept {
    constexpr std::size_t n = kFrameSize;

    // Enrollment recording taps the microphone signal the model sees (after gain and high-pass).
    if (recordState_.load(std::memory_order_acquire) == static_cast<int>(RecordingState::Recording)) {
        const std::size_t length = recordLength_.load(std::memory_order_relaxed);
        const std::size_t position = recordPosition_.load(std::memory_order_relaxed);
        const std::size_t take = std::min(n, length - std::min(position, length));
        std::memcpy(recordBuffer_.data() + position, frame, take * sizeof(float));
        float peak = recordPeak_.load(std::memory_order_relaxed) * 0.97f;
        for (std::size_t i = 0; i < take; ++i) {
            peak = std::max(peak, std::fabs(frame[i]));
        }
        recordPeak_.store(peak, std::memory_order_relaxed);
        recordPosition_.store(position + take, std::memory_order_relaxed);
        if (position + take >= length) {
            int expected = static_cast<int>(RecordingState::Recording);
            recordState_.compare_exchange_strong(expected, static_cast<int>(RecordingState::Ready), std::memory_order_release,
                                                 std::memory_order_relaxed);
        }
    }

    info.enabled = enabled;
    if (!enabled) {
        if (wasEnabled_) {
            // Back to the microphone after isolated audio or silence: fade it in over one frame.
            for (std::size_t i = 0; i < n; ++i) {
                frame[i] *= static_cast<float>(i + 1) / static_cast<float>(n);
            }
        }
        wasEnabled_ = false;
        lastFrameDry_ = true;
        info.delivering = false;
        info.latencySamples = 0;
        metrics_.delivering.store(false, std::memory_order_relaxed);
        return;
    }

    // From here on the frame leaves as isolated audio or silence - never as the microphone signal.
    const std::size_t dryFade = lastFrameDry_ ? kDryFadeSamples : 0; // click-free switch from the microphone
    lastFrameDry_ = false;
    if (!wasEnabled_) {
        wasEnabled_ = true;
        requestRestart();
    }
    const std::uint32_t generation = modelGeneration_.load(std::memory_order_acquire);
    if (generation != seenGeneration_) {
        seenGeneration_ = generation; // a new voice (or none): start a fresh stream with it
        requestRestart();
    }
    if (workerFailed_.load(std::memory_order_acquire) && !restartPending_) {
        requestRestart(); // inference failed: start over, silent meanwhile
    }
    if (restartPending_ && ackedEpoch_.load(std::memory_order_acquire) == epoch_) {
        outRing_.skip(outRing_.available()); // anything still queued predates the restart
        restartPending_ = false;
        priming_ = true;
        pushed_ = popped_ = 0;
        minFill_ = SIZE_MAX;
        fillWindowFrames_ = 0;
        pendingTrim_ = 0;
        if (marginSamples_ == 0) {
            marginSamples_ = kInitialMargin;
        }
    }

    bool delivered = false;
    const int k = activeFramesPerRun_.load(std::memory_order_acquire);
    if (!restartPending_ && k > 0 && modelReady_.load(std::memory_order_acquire)) {
        if (!inRing_.tryPush(frame, n)) {
            metrics_.overruns.fetch_add(1, std::memory_order_relaxed); // worker stalled for a second
            requestRestart();
        } else {
            pushed_ += static_cast<long long>(n);
            SetEvent(wakeEvent_);
            startThreshold_ = n + burstSamples48k(k) + marginSamples_;
            const std::size_t available = outRing_.available();
            if (priming_ && available >= startThreshold_) {
                priming_ = false;
                fadeIn_ = 0;
            }
            if (!priming_) {
                if (available >= n) {
                    std::size_t fill = available;
                    if (pendingTrim_ > 0 && quietFrames_ >= 2 && fill >= n + pendingTrim_) {
                        // Drop surplus delay while the voice is silent; the next frame fades in.
                        outRing_.pop(trimScratch_.data(), pendingTrim_);
                        popped_ += static_cast<long long>(pendingTrim_);
                        fill -= pendingTrim_;
                        pendingTrim_ = 0;
                        fadeIn_ = 0;
                        minFill_ = SIZE_MAX;
                        fillWindowFrames_ = 0;
                    }
                    minFill_ = std::min(minFill_, fill);
                    if (++fillWindowFrames_ >= kTrimWindowFrames) {
                        const std::size_t keep = n + marginSamples_;
                        const std::size_t slack = minFill_ > keep ? minFill_ - keep : 0;
                        pendingTrim_ = slack >= kMinTrim ? std::min(slack, kMaxTrim) : 0;
                        minFill_ = SIZE_MAX;
                        fillWindowFrames_ = 0;
                    }
                    outRing_.pop(scratch_.data(), n);
                    popped_ += static_cast<long long>(n);
                    delivered = true;
                } else {
                    // The worker is late. This frame is silence (not the microphone) and the isolated voice
                    // continues one frame later; keep a larger cushion from now on.
                    metrics_.underruns.fetch_add(1, std::memory_order_relaxed);
                    pendingTrim_ = 0;
                    minFill_ = SIZE_MAX;
                    fillWindowFrames_ = 0;
                    marginSamples_ = std::min(marginSamples_ + kMarginStep, kMaxMargin);
                }
            }
        }
    }

    if (delivered) {
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            float g = 1.0f;
            if (fadeIn_ < n) {
                g = static_cast<float>(++fadeIn_) / static_cast<float>(n); // 10 ms fade-in after silence
            }
            frame[i] = scratch_[i] * g;
            peak = std::max(peak, std::fabs(scratch_[i]));
        }
        quietFrames_ = peak < kQuietPeak ? quietFrames_ + 1 : 0;
        lastLatencySamples_ = static_cast<int>(pushed_ - popped_);
        metrics_.isolatedFrames.fetch_add(1, std::memory_order_relaxed);
        metrics_.latencyMs.store(static_cast<float>(lastLatencySamples_) * 1000.0f / kSampleRate, std::memory_order_relaxed);
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            frame[i] = i < dryFade ? frame[i] * (1.0f - static_cast<float>(i + 1) / static_cast<float>(dryFade)) : 0.0f;
        }
        fadeIn_ = 0;
        quietFrames_ = 0;
        metrics_.silencedFrames.fetch_add(1, std::memory_order_relaxed);
    }
    metrics_.delivering.store(delivered, std::memory_order_relaxed);
    info.delivering = delivered;
    info.latencySamples = lastLatencySamples_;
}

// ---- Worker thread ---------------------------------------------------------------------------------

void VoiceIsolator::workerMain() {
    setCurrentThreadName(L"VoiceIsolator worker");
    preferPerformanceCores();
    ScopedMmcss mmcss(L"Pro Audio");
    while (!workerQuit_.load(std::memory_order_acquire)) {
        WaitForSingleObject(wakeEvent_, 20);
        if (requestedEpoch_.load(std::memory_order_acquire) != ackedEpoch_.load(std::memory_order_relaxed)) {
            workerReset();
        }
        workerProcess();
    }
    model_.reset(); // release the inference session on the thread that used it
}

void VoiceIsolator::workerReset() {
    const std::uint32_t epoch = requestedEpoch_.load(std::memory_order_acquire);
    inRing_.skip(inRing_.available());

    std::unique_ptr<voxlock::IChunkModel> incoming;
    bool clear = false;
    {
        std::lock_guard lock(modelMutex_);
        incoming = std::move(pendingModel_);
        clear = pendingClear_;
        pendingClear_ = false;
    }
    if (clear) {
        model_.reset();
    }
    if (incoming) {
        model_ = std::make_unique<TimedModel>(std::move(incoming), metrics_);
    }
    int k = 0;
    if (model_) {
        k = model_->framesPerRun();
        if (!stream_ || stream_->framesPerRun() != k) {
            stream_ = std::make_unique<voxlock::Stream>(k, kMaxPush16);
            if (out16_.size() < static_cast<std::size_t>(stream_->maxOutputSamples())) {
                out16_.resize(static_cast<std::size_t>(stream_->maxOutputSamples()));
                out48_.resize(out16_.size() * 3 + 64);
                up_.configure(voxlock::kSampleRate, kSampleRate, out16_.size(), kUpHalfTaps);
            }
        }
        stream_->reset();
    }
    down_.reset();
    up_.reset();
    workerFailed_.store(false, std::memory_order_release);
    activeFramesPerRun_.store(k, std::memory_order_release);
    ackedEpoch_.store(epoch, std::memory_order_release);
}

void VoiceIsolator::workerProcess() {
    if (!model_ || !stream_ || workerFailed_.load(std::memory_order_relaxed)) {
        inRing_.skip(inRing_.available());
        return;
    }
    for (;;) {
        const std::size_t count = inRing_.pop(in48_.data(), in48_.size());
        if (count == 0) {
            return;
        }
        std::size_t offset = 0;
        while (offset < count) {
            offset += down_.write(in48_.data() + offset, count - offset);
            std::size_t produced16 = 0;
            while ((produced16 = down_.read(buf16_.data(), buf16_.size())) > 0) {
                const int isolated = stream_->push(*model_, buf16_.data(), static_cast<int>(produced16), out16_.data());
                bool finite = isolated >= 0;
                for (int i = 0; finite && i < isolated; ++i) {
                    finite = std::isfinite(out16_[static_cast<std::size_t>(i)]);
                }
                if (!finite) {
                    // Fail closed: the capture thread outputs silence and restarts the stream.
                    if (metrics_.modelFailures.fetch_add(1, std::memory_order_relaxed) == 0) {
                        logError("Voice isolation inference failed; output muted until the stream restarts");
                    }
                    workerFailed_.store(true, std::memory_order_release);
                    return;
                }
                std::size_t upOffset = 0;
                const auto total = static_cast<std::size_t>(isolated);
                while (upOffset < total) {
                    upOffset += up_.write(out16_.data() + upOffset, total - upOffset);
                    const std::size_t produced48 = up_.read(out48_.data(), out48_.size());
                    if (produced48 > 0 && !outRing_.tryPush(out48_.data(), produced48)) {
                        metrics_.overruns.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }
    }
}

} // namespace vs
