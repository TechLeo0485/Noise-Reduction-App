#include "audio/isolation/VoiceIsolator.h"

#include "ai/voxlock/VoxLockEnrollment.h"
#include "core/RealtimeThread.h"
#include "diagnostics/Logger.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>

namespace vs {

namespace {

constexpr std::size_t kFrameSizeConst = VoiceIsolator::kFrameSize;
constexpr std::size_t kRingCapacity = 48000;   // 1 s at 48 kHz in each direction
constexpr std::size_t kRecordRingCapacity = 48000 * 5; // enrollment recording: 5 s between pumpRecording() calls
constexpr std::size_t kRecordChunk = 4800;     // 48 kHz samples resampled at a time while recording
constexpr std::size_t kWorkerBlock = 960;      // 48 kHz samples taken from the input ring at a time
constexpr int kMaxPush16 = 1024;               // 16 kHz samples per Stream::push
constexpr std::size_t kMaxStreamOutput = 4096; // >= Stream::maxOutputSamples() for K <= 8
constexpr int kDownHalfTaps = 48;              // 48 -> 16 kHz anti-alias filter, 1 ms look-ahead
constexpr int kUpHalfTaps = 16;                // 16 -> 48 kHz interpolation, 1 ms look-ahead
constexpr std::size_t kInitialMargin = 960;    // 20 ms safety fill: covers inference spikes (measured max ~18 ms)
constexpr std::size_t kMarginStep = 240;       // added after every underrun ...
constexpr std::size_t kMaxMargin = 2880;       // ... up to 60 ms
constexpr std::size_t kDryFadeSamples = 96;    // 2 ms ramp-down of the microphone when isolation turns on
constexpr int kTrimWindowFrames = 100;         // 1 s of fill observations per latency-trim decision
constexpr std::size_t kMinTrim = 96;           // ignore slack below 2 ms
constexpr std::size_t kMaxTrim = kFrameSizeConst / 2; // at most 5 ms per step, so a clipped onset is inaudible
constexpr float kQuietPeak = 0.003f;           // about -50 dBFS: a trim here is inaudible
constexpr int kMarginRelaxFrames = 2000;       // 20 s without a late chunk: give back one cushion step
constexpr int kMinMaxLatency = 2400;           // the delay limit is never set below 50 ms ...
constexpr int kMaxMaxLatency = 12000;          // ... or above 250 ms
constexpr int kThreads = 2;                    // intra-op threads per inference (measured: 4 is no faster)
constexpr double kHopMs = 1000.0 * voxlock::kHop / voxlock::kSampleRate;
constexpr int kMaxFramesPerRun = 5;            // 40 ms chunks: the largest step the monitor escalates to
constexpr std::uint64_t kUnderrunsToAdapt = 4; // late chunks within kAdaptWindow that trigger a larger chunk
constexpr auto kAdaptWindow = std::chrono::seconds(10);
constexpr std::uint64_t kFailuresToStop = 3;   // inference errors within kFailureWindow that end isolation
constexpr auto kFailureWindow = std::chrono::seconds(30);
constexpr int kTestFramesPerRun = 8;           // offline voice-sample tests: large chunks, fastest per second of audio
constexpr std::size_t kAppendGapSamples = voxlock::kSampleRate * 3 / 10; // 0.3 s pause between recording sessions
constexpr std::size_t kCheckRingCapacity = voxlock::kSampleRate * 2;       // 2 s of isolated output for the checker
constexpr std::size_t kCheckBlock = 1600;      // 16 kHz samples the checker takes at a time
constexpr float kGateStep = 1.0f / 320.0f;     // the voice check mutes over 20 ms ...
constexpr float kGateOpenStep = 1.0f / 80.0f;  // ... and unmutes over 5 ms (keeps the start of a word)
constexpr int kGateOpen = static_cast<int>(voxlock::VoiceCheck::Gate::Open);
constexpr int kGateOnSpeech = static_cast<int>(voxlock::VoiceCheck::Gate::OpenOnSpeech);

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
      inRing_(kRingCapacity), outRing_(kRingCapacity), checkRing_(kCheckRingCapacity), recordRing_(kRecordRingCapacity),
      recordChunk_(kRecordChunk) {
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
    if (checkEvent_ != nullptr) {
        SetEvent(checkEvent_);
    }
    if (checker_.joinable()) {
        checker_.join();
    }
    if (wakeEvent_ != nullptr) {
        CloseHandle(wakeEvent_);
    }
    if (checkEvent_ != nullptr) {
        CloseHandle(checkEvent_);
    }
}

// ---- Control --------------------------------------------------------------------------------------

void VoiceIsolator::startThreads() {
    if (worker_.joinable()) {
        return;
    }
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    checkEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    worker_ = std::thread([this] { workerMain(); });
    checker_ = std::thread([this] { checkerMain(); });
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
        if (!loaderWake_.wait_for(lock, std::chrono::seconds(1), [this] { return quit_ || !loaderTasks_.empty(); })) {
            lock.unlock();
            try {
                monitor();
            } catch (const std::exception& e) {
                logError(std::string("Voice isolation monitor: ") + e.what());
            }
            lock.lock();
            continue;
        }
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
        if (!busy) {
            status_.prepareProgress = 0.0;
        }
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
        {
            std::string speakerError;
            auto speaker = runtime_->createSpeakerModel(speakerError);
            if (!speaker) {
                logWarning("Voice check unavailable (other voices are only reduced by the isolation model): " + speakerError);
            }
            std::lock_guard lock(speakerMutex_);
            speakerModel_ = std::move(speaker);
        }
        std::error_code ec;
        if (!enrollmentFile_.empty() && std::filesystem::exists(enrollmentFile_, ec)) {
            std::vector<float> voice;
            if (voxlock::loadVoiceSample(enrollmentFile_, voice, error) && prepareVoice(voice, error)) {
                std::vector<float> recording;
                std::string ignored;
                updateSpeakerReference(voxlock::loadVoiceSample(recordingFile(), recording, ignored) ? recording : voice);
                std::string microphone;
                std::ifstream in(microphoneFile());
                std::getline(in, microphone);
                std::lock_guard lock(statusMutex_);
                status_.enrolled = true;
                status_.enrolledMicrophone = microphone;
                status_.savedSeconds = savedRecordingSeconds();
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

void VoiceIsolator::enroll(std::vector<float> audio16k, std::string microphone, bool append) {
    if (state() == IsolationState::NotBuilt) {
        return;
    }
    post([this, audio = std::move(audio16k), microphone = std::move(microphone), append]() mutable {
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
        // Adding more: the new part goes after what is saved, with a short pause in between.
        if (append && !enrollmentFile_.empty()) {
            std::vector<float> saved;
            std::string ignored;
            if (voxlock::loadVoiceSample(recordingFile(), saved, ignored) ||
                voxlock::loadVoiceSample(enrollmentFile_, saved, ignored)) {
                saved.resize(saved.size() + kAppendGapSamples, 0.0f);
                saved.insert(saved.end(), audio.begin(), audio.end());
                const auto limit = static_cast<std::size_t>(kRecordMaxSeconds * voxlock::kSampleRate);
                if (saved.size() > limit) { // keep the newest 30 minutes
                    saved.erase(saved.begin(), saved.begin() + static_cast<std::ptrdiff_t>(saved.size() - limit));
                }
                logInfo("Voice recording: adding " + std::to_string(audio.size() / voxlock::kSampleRate) + " s to " +
                        std::to_string((saved.size() - audio.size()) / voxlock::kSampleRate) + " s saved");
                audio = std::move(saved);
            }
        }
        setStatus(before, "Preparing your voice...", true);
        const std::vector<float> sample = chooseVoiceSample(audio, before);
        if (!prepareVoice(sample, error)) {
            logError("Voice enrollment failed: " + error);
            setStatus(before, "Voice enrollment failed: " + error, false);
            return;
        }
        updateSpeakerReference(audio);
        // Saved: the 3 s the model uses (loads exactly as enrolled) and the whole recording (for adding more).
        if (!enrollmentFile_.empty() && !voxlock::saveVoiceSample(enrollmentFile_, sample, error)) {
            logError("Saving the voice sample failed: " + error);
            setStatus(IsolationState::Ready, "Isolating your voice, but it could not be saved for next time: " + error, false);
            return;
        }
        if (!enrollmentFile_.empty() && !voxlock::saveRecording(recordingFile(), audio, error)) {
            logWarning("Saving the full voice recording failed: " + error);
        }
        if (!enrollmentFile_.empty()) {
            std::ofstream(microphoneFile(), std::ios::trunc) << microphone << '\n';
        }
        logInfo("Voice enrolled" + (microphone.empty() ? std::string() : " with " + microphone));
        std::lock_guard lock(statusMutex_);
        status_.enrolled = true;
        status_.enrolledMicrophone = microphone;
        status_.savedSeconds = static_cast<double>(audio.size()) / voxlock::kSampleRate;
    });
}

std::vector<float> VoiceIsolator::chooseVoiceSample(const std::vector<float>& audio16k, IsolationState state) {
    const voxlock::EnrollmentPlan plan = voxlock::planEnrollment(audio16k);
    if (plan.candidates.empty()) {
        return audio16k; // shorter than 3 s: prepareVoice() reports it
    }
#if defined(VOICESTUDIO_HAS_VOXLOCK)
    if (!testFactory_ && runtime_ && plan.candidates.size() > 1 && !plan.tests.empty()) {
        const auto start = std::chrono::steady_clock::now();
        const auto isolate = [this](const std::vector<float>& sample, const std::vector<std::vector<float>>& inputs,
                                    std::vector<std::vector<float>>& outputs) {
            voxlock::EnrollmentTensors tensors;
            std::string error;
            if (!runtime_->enroll(sample, tensors, error)) {
                logWarning("Voice sample test: " + error);
                return false;
            }
            voxlock::ChunkModelOptions options;
            options.framesPerRun = kTestFramesPerRun;
            options.threads = kThreads;
            const auto model = runtime_->createChunkModel(tensors, options, error);
            if (!model) {
                logWarning("Voice sample test: " + error);
                return false;
            }
            // Offline: push each test segment, then enough silence to get its last samples out.
            constexpr int kPush = 1600;
            voxlock::Stream stream(kTestFramesPerRun, kPush);
            std::vector<float> out(static_cast<std::size_t>(stream.maxOutputSamples()));
            const std::vector<float> tail(static_cast<std::size_t>(voxlock::kNfft + kTestFramesPerRun * voxlock::kHop), 0.0f);
            outputs.clear();
            for (const std::vector<float>& input : inputs) {
                stream.reset();
                std::vector<float> result;
                for (const std::vector<float>* part : {&input, &tail}) {
                    for (std::size_t i = 0; i < part->size(); i += kPush) {
                        const int count = static_cast<int>(std::min<std::size_t>(kPush, part->size() - i));
                        const int n = stream.push(*model, part->data() + i, count, out.data());
                        if (n < 0) {
                            return false;
                        }
                        result.insert(result.end(), out.begin(), out.begin() + n);
                    }
                }
                result.resize(input.size(), 0.0f);
                outputs.push_back(std::move(result));
            }
            return true;
        };
        const auto progress = [this, state](std::size_t index, std::size_t count) {
            {
                std::lock_guard lock(statusMutex_);
                status_.prepareProgress = static_cast<double>(index) / static_cast<double>(count + 1);
            }
            setStatus(state,
                      "Testing which parts of your recording recognise you best (" + std::to_string(index * 100 / count) +
                          "%)...",
                      true);
        };
        std::vector<double> scores;
        // Several candidates at once (2 threads each), leaving cores for the voice that keeps running meanwhile.
        const int parallel = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 4u, 1u, 4u));
        const std::size_t best = voxlock::chooseCandidate(audio16k, plan, isolate, progress, &scores, parallel);
        std::string log = "Voice sample chosen: " + plan.candidates[best].label + " (" +
                          std::to_string(plan.candidates.size()) + " candidates, " + std::to_string(plan.tests.size()) +
                          " test segments, " +
                          std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - start)
                                             .count()) +
                          " ms; scores";
        for (std::size_t i = 0; i < scores.size(); ++i) {
            log += " " + plan.candidates[i].label + "=" + std::to_string(scores[i]).substr(0, 6);
        }
        logInfo(log + ")");
        {
            std::lock_guard lock(statusMutex_);
            status_.prepareProgress = static_cast<double>(plan.candidates.size()) / static_cast<double>(plan.candidates.size() + 1);
        }
        setStatus(state, "Measuring this computer's speed...", true);
        return plan.candidates[best].sample;
    }
#else
    (void)state;
#endif
    return plan.candidates.front().sample;
}

double VoiceIsolator::savedRecordingSeconds() const {
    // 16 kHz 16-bit mono with a 44-byte header (saveRecording()); before the full recording was kept, 3 s.
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(recordingFile(), ec);
    if (!ec && bytes > 44) {
        return static_cast<double>(bytes - 44) / (2.0 * voxlock::kSampleRate);
    }
    return std::filesystem::exists(enrollmentFile_, ec) ? 3.0 : 0.0;
}

void VoiceIsolator::deleteEnrollment() {
    if (state() == IsolationState::NotBuilt) {
        return;
    }
    post([this] {
        std::error_code ec;
        if (!enrollmentFile_.empty()) {
            std::filesystem::remove(enrollmentFile_, ec);
            std::error_code ignored;
            std::filesystem::remove(microphoneFile(), ignored);
            std::filesystem::remove(recordingFile(), ignored);
        }
        tensors_ = {};
        haveTensors_ = false;
        updateSpeakerReference({});
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
            status_.enrolledMicrophone.clear();
            status_.framesPerRun = 0;
            status_.savedSeconds = 0.0;
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

int pickProcessor(const std::vector<ProcessorTrial>& trials) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(trials.size()); ++i) {
        const ProcessorTrial& t = trials[static_cast<std::size_t>(i)];
        if (!t.error.empty() || t.framesPerRun <= 0) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        const ProcessorTrial& b = trials[static_cast<std::size_t>(best)];
        const double headroom = t.p95Ms / (t.framesPerRun * kHopMs);
        const double bestHeadroom = b.p95Ms / (b.framesPerRun * kHopMs);
        bool better = false;
        if (t.realTime != b.realTime) {
            better = t.realTime;
        } else if (t.realTime && t.framesPerRun != b.framesPerRun) {
            better = t.framesPerRun < b.framesPerRun; // lower latency
        } else {
            better = headroom < bestHeadroom - 0.02; // clearly more headroom; on a tie the earlier (CPU) stays
        }
        if (better) {
            best = i;
        }
    }
    return best;
}

void VoiceIsolator::setMaxLatencyMs(double ms) noexcept {
    const auto samples = static_cast<int>(std::lround(ms * kSampleRate / 1000.0));
    maxLatencySamples_.store(std::clamp(samples, kMinMaxLatency, kMaxMaxLatency), std::memory_order_relaxed);
}

double VoiceIsolator::maxLatencyMs() const noexcept {
    return maxLatencySamples_.load(std::memory_order_relaxed) * 1000.0 / kSampleRate;
}

void VoiceIsolator::setProcessors(std::vector<IsolationProcessor> available, std::string preferred) {
    {
        std::lock_guard lock(processorsMutex_);
        bool same = preferred == preferredProcessor_ && available.size() == processors_.size();
        for (std::size_t i = 0; same && i < available.size(); ++i) {
            same = available[i].id == processors_[i].id && available[i].dmlDevice == processors_[i].dmlDevice;
        }
        if (same) {
            return;
        }
        processors_ = std::move(available);
        preferredProcessor_ = std::move(preferred);
    }
    if (state() == IsolationState::NotBuilt) {
        return;
    }
    post([this] {
        if (!haveTensors_ || testFactory_) {
            return; // used by the next voice preparation
        }
        const IsolationState before = state();
        setStatus(before, "Measuring this computer's speed...", true);
        std::string error;
        if (!calibrateAndPublish(tensors_, error)) {
            logError("Voice isolation: re-calibration failed: " + error);
            setStatus(before, "Voice isolation could not switch processor: " + error, false);
        }
    });
}

bool VoiceIsolator::calibrateAndPublish(const voxlock::EnrollmentTensors& tensors, std::string& error) {
#if defined(VOICESTUDIO_HAS_VOXLOCK)
    std::vector<IsolationProcessor> candidates;
    std::string preferred;
    {
        std::lock_guard lock(processorsMutex_);
        preferred = preferredProcessor_;
        for (const IsolationProcessor& p : processors_) {
            if (preferred == "auto" || p.id == preferred) {
                candidates.push_back(p);
            }
        }
    }
    const bool cpuListed = std::any_of(candidates.begin(), candidates.end(), [](const auto& p) { return p.dmlDevice < 0; });
    if (!cpuListed) {
        // Measured too: the fallback if the chosen GPU cannot run the model, and the comparison that keeps a slow
        // GPU from adding delay.
        candidates.push_back({"cpu", "CPU", -1});
    }

    // For each processor: the smallest chunk K that keeps a safe margin. Each extra hop adds 8 ms of latency.
    std::vector<ProcessorTrial> trials;
    std::vector<std::unique_ptr<voxlock::IChunkModel>> models;
    std::mt19937 rng(1234);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        const IsolationProcessor& processor = candidates[c];
        ProcessorTrial trial;
        trial.id = processor.id;
        trial.name = processor.name;
        std::unique_ptr<voxlock::IChunkModel> best;
        for (int k : {2, 3, 4, 5}) {
            voxlock::ChunkModelOptions options;
            options.framesPerRun = k;
            options.threads = processor.dmlDevice >= 0 ? 1 : kThreads;
            options.dmlDevice = processor.dmlDevice;
            std::string createError;
            auto model = runtime_->createChunkModel(tensors, options, createError);
            if (!model) {
                trial.error = createError;
                break;
            }
            std::vector<float> wav(static_cast<std::size_t>(voxlock::chunkInputSamples(k)));
            for (float& v : wav) {
                v = noise(rng);
            }
            std::vector<float> frames(static_cast<std::size_t>(k) * voxlock::kNfft);
            std::vector<float> h[2] = {std::vector<float>(voxlock::kStateSize), std::vector<float>(voxlock::kStateSize)};
            std::vector<float> cs[2] = {std::vector<float>(voxlock::kStateSize), std::vector<float>(voxlock::kStateSize)};
            std::vector<double> times;
            int s = 0;
            bool failed = false;
            for (int i = 0; i < 46; ++i) {
                const std::int64_t t0 = HighResClock::now();
                if (!model->run(wav.data(), h[s].data(), cs[s].data(), frames.data(), h[s ^ 1].data(), cs[s ^ 1].data())) {
                    failed = true;
                    break;
                }
                s ^= 1;
                if (i >= 6) { // the first runs include warm-up (and shader compilation on GPUs)
                    times.push_back(HighResClock::ticksToMilliseconds(HighResClock::now() - t0));
                }
            }
            if (failed) {
                trial.error = "inference failed during calibration";
                break;
            }
            std::sort(times.begin(), times.end());
            const double budget = k * kHopMs;
            trial.framesPerRun = k;
            trial.p50Ms = times[times.size() / 2];
            trial.p95Ms = times[times.size() * 95 / 100];
            trial.realTime = trial.p50Ms <= 0.75 * budget && trial.p95Ms <= budget;
            best = std::move(model);
            logInfo("Voice isolation calibration on " + processor.name + ", K=" + std::to_string(k) + ": p50 " +
                    std::to_string(trial.p50Ms) + " ms, p95 " + std::to_string(trial.p95Ms) + " ms per " +
                    std::to_string(budget) + " ms of audio");
            if (trial.realTime) {
                break;
            }
        }
        if (!trial.error.empty()) {
            trial.framesPerRun = 0;
            best.reset();
            logWarning("Voice isolation cannot use " + processor.name + ": " + trial.error);
        }
        trials.push_back(trial);
        models.push_back(std::move(best));
    }

    int chosen = pickProcessor(trials);
    bool cpuFaster = false;
    if (preferred != "auto" && chosen >= 0) {
        // The chosen processor stays unless the CPU gives clearly less delay: it keeps up where the chosen one
        // does not, or it needs smaller chunks. (Older GPUs are often slower than the CPU for this model.)
        for (std::size_t i = 0; i < trials.size(); ++i) {
            const ProcessorTrial& mine = trials[i];
            if (mine.id != preferred || !mine.error.empty() || mine.framesPerRun <= 0) {
                continue;
            }
            const ProcessorTrial& best = trials[static_cast<std::size_t>(chosen)];
            const bool clearlyWorse = (best.realTime && !mine.realTime) ||
                                      (best.realTime == mine.realTime && best.framesPerRun < mine.framesPerRun);
            if (clearlyWorse) {
                cpuFaster = true;
            } else {
                chosen = static_cast<int>(i);
            }
        }
    }
    if (chosen < 0) {
        error = trials.empty() ? "no processor available" : trials.front().error;
        return false;
    }
    const ProcessorTrial& t = trials[static_cast<std::size_t>(chosen)];
    const IsolationProcessor& processor = candidates[static_cast<std::size_t>(chosen)];
    publish(std::move(models[static_cast<std::size_t>(chosen)]), t.framesPerRun);
    currentDmlDevice_ = processor.dmlDevice;
    health_.clear();
    logInfo("Voice isolation runs on " + processor.name + " with " + std::to_string(t.framesPerRun * 8) + " ms chunks" +
            (preferred == "auto" ? " (chosen automatically)" : ""));
    {
        std::lock_guard lock(statusMutex_);
        status_.framesPerRun = t.framesPerRun;
        status_.threads = processor.dmlDevice >= 0 ? 1 : kThreads;
        status_.calibrationP50Ms = t.p50Ms;
        status_.calibrationP95Ms = t.p95Ms;
        status_.processorId = processor.id;
        status_.processorName = processor.name;
        status_.trials = trials;
    }
    std::string where = processor.dmlDevice >= 0 ? " on " + processor.name : std::string();
    if (cpuFaster) {
        where = " on the CPU: it is faster than the chosen graphics card on this computer, so there is less delay";
    } else if (preferred != "auto" && preferred != processor.id) {
        where += " (the chosen processor could not run it)";
    }
    if (!t.realTime) {
        setStatus(IsolationState::Ready,
                  "This computer is barely fast enough for real-time isolation" + where + "; expect short silent gaps.",
                  false);
    } else {
        setStatus(IsolationState::Ready, "Isolating your voice" + where + ".", false);
    }
    return true;
#else
    (void)tensors;
    error = "not included in this build";
    return false;
#endif
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

    if (!calibrateAndPublish(tensors, error)) {
        return false;
    }
    tensors_ = std::move(tensors);
    haveTensors_ = true;
    return true;
#else
    (void)audio16k;
    error = "not included in this build";
    return false;
#endif
}

std::filesystem::path VoiceIsolator::recordingFile() const {
    return enrollmentFile_.parent_path() / L"my_voice_recording.wav";
}

std::filesystem::path VoiceIsolator::microphoneFile() const {
    std::filesystem::path path = enrollmentFile_;
    path += L".microphone.txt";
    return path;
}

// Runs on the loader thread about once per second. Turns persistent problems into actions instead of
// leaving the user with repeated dropouts or an unexplained silent microphone:
//  - the worker keeps falling behind (several late chunks in 10 s): rebuild the session with a larger
//    chunk (+8 ms latency per step, up to 40 ms chunks);
//  - inference keeps failing: stop with an error message (the output stays silent while enabled).
void VoiceIsolator::monitor() {
    if (state() != IsolationState::Ready) {
        health_.clear();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const HealthSample sample{now, metrics_.underruns.load(std::memory_order_relaxed),
                              metrics_.modelFailures.load(std::memory_order_relaxed)};
    health_.push_back(sample);
    while (!health_.empty() && now - health_.front().time > kFailureWindow) {
        health_.pop_front();
    }

    if (sample.failures - health_.front().failures >= kFailuresToStop) {
        logError("Voice isolation stopped: repeated inference errors");
        setStatus(IsolationState::Error,
                  "Voice isolation keeps failing on this computer. While it is switched on your microphone stays "
                  "silent: switch it off, or restart VoiceStudio.",
                  false);
        health_.clear();
        return;
    }

    std::uint64_t underrunsBefore = sample.underruns;
    for (const HealthSample& h : health_) {
        if (now - h.time <= kAdaptWindow) {
            underrunsBefore = h.underruns;
            break;
        }
    }
    const int k = status().framesPerRun;
    if (sample.underruns - underrunsBefore < kUnderrunsToAdapt || k <= 0 || k >= kMaxFramesPerRun) {
        return;
    }
    const int next = k + 1;
    std::unique_ptr<voxlock::IChunkModel> model;
    std::string error;
    if (testFactory_) {
        model = testFactory_(next);
    } else if (haveTensors_ && runtime_) {
#if defined(VOICESTUDIO_HAS_VOXLOCK)
        voxlock::ChunkModelOptions options;
        options.framesPerRun = next;
        options.threads = currentDmlDevice_ >= 0 ? 1 : kThreads;
        options.dmlDevice = currentDmlDevice_;
        model = runtime_->createChunkModel(tensors_, options, error);
#endif
    }
    if (!model) {
        logWarning("Voice isolation could not switch to a larger chunk: " + error);
        health_.clear();
        return;
    }
    publish(std::move(model), next);
    logWarning("Voice isolation fell behind (" + std::to_string(sample.underruns - underrunsBefore) +
               " late chunks in 10 s); chunk size raised to " + std::to_string(next * 8) + " ms");
    {
        std::lock_guard lock(statusMutex_);
        status_.framesPerRun = next;
        ++status_.adaptations;
    }
    setStatus(IsolationState::Ready,
              "This computer was too busy for the previous setting; isolation now works in " + std::to_string(next * 8) +
                  " ms steps (" + std::to_string((next - k) * 8) + " ms more delay) to avoid dropouts.",
              false);
    health_.clear();
}

// ---- Enrollment recording ------------------------------------------------------------------------------

bool VoiceIsolator::startRecording(double maxSeconds) {
    if (recordingState() == RecordingState::Recording) {
        return false;
    }
    const auto length = std::clamp<std::size_t>(static_cast<std::size_t>(maxSeconds * kSampleRate),
                                                static_cast<std::size_t>(kSampleRate) * 3,
                                                static_cast<std::size_t>(kRecordMaxSeconds * kSampleRate));
    // The capture thread only pushes while Recording, so the ring has no producer here.
    recordRing_.skip(recordRing_.available());
    recordResampler_.configure(kSampleRate, voxlock::kSampleRate, kRecordChunk + kDownHalfTaps + 16, kDownHalfTaps);
    recorded16k_.clear();
    recorded16k_.reserve(static_cast<std::size_t>(voxlock::kSampleRate) * 60);
    recordLength_.store(length, std::memory_order_relaxed);
    recordPosition_.store(0, std::memory_order_relaxed);
    recordDropped_.store(0, std::memory_order_relaxed);
    recordPeak_.store(0.0f, std::memory_order_relaxed);
    recordState_.store(static_cast<int>(RecordingState::Recording), std::memory_order_release);
    return true;
}

void VoiceIsolator::stopRecording() noexcept {
    int expected = static_cast<int>(RecordingState::Recording);
    recordState_.compare_exchange_strong(expected, static_cast<int>(RecordingState::Ready), std::memory_order_acq_rel);
}

void VoiceIsolator::cancelRecording() noexcept {
    recordState_.store(static_cast<int>(RecordingState::Idle), std::memory_order_release);
}

double VoiceIsolator::recordingSeconds() const noexcept {
    return static_cast<double>(recordPosition_.load(std::memory_order_relaxed)) / kSampleRate;
}

float VoiceIsolator::recordingProgress() const noexcept {
    const std::size_t length = recordLength_.load(std::memory_order_relaxed);
    return length > 0 ? static_cast<float>(recordPosition_.load(std::memory_order_relaxed)) / static_cast<float>(length) : 0.0f;
}

void VoiceIsolator::pumpRecording() {
    if (recordingState() == RecordingState::Idle || !recordResampler_.isConfigured()) {
        return;
    }
    std::vector<float> out16(kRecordChunk / 3 + 16);
    for (;;) {
        const std::size_t room = std::min(recordChunk_.size(), recordResampler_.freeInputSpace());
        const std::size_t n = recordRing_.pop(recordChunk_.data(), room);
        if (n == 0) {
            break;
        }
        recordResampler_.write(recordChunk_.data(), n);
        const std::size_t produced = recordResampler_.read(out16.data(), out16.size());
        recorded16k_.insert(recorded16k_.end(), out16.begin(), out16.begin() + static_cast<std::ptrdiff_t>(produced));
    }
}

std::vector<float> VoiceIsolator::takeRecording16k() {
    std::vector<float> out;
    if (recordingState() != RecordingState::Ready) { // acquire: the capture thread's writes are visible
        return out;
    }
    pumpRecording();
    // Flush the anti-alias filter's look-ahead so the last millisecond is not lost.
    const std::vector<float> tail(kDownHalfTaps + 4, 0.0f);
    recordResampler_.write(tail.data(), tail.size());
    std::vector<float> rest(tail.size());
    const std::size_t produced = recordResampler_.read(rest.data(), rest.size());
    recorded16k_.insert(recorded16k_.end(), rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(produced));
    const std::size_t expected = recordPosition_.load(std::memory_order_relaxed) / 3;
    if (recorded16k_.size() > expected) {
        recorded16k_.resize(expected);
    }
    out.swap(recorded16k_);
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
        if (!recordRing_.tryPush(frame, take)) {
            recordDropped_.fetch_add(take, std::memory_order_relaxed);
        }
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
        framesSinceUnderrun_ = 0;
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
                    // Hard limit: the isolated voice never falls further behind than the delay budget allows.
                    // Normally the slow trim below keeps it well under; this catches bursts after a stall.
                    const long long excess = pushed_ - popped_ - maxLatencySamples_.load(std::memory_order_relaxed);
                    if (excess > 0 && fill > n) {
                        const std::size_t drop = std::min(fill - n, static_cast<std::size_t>(excess));
                        outRing_.skip(drop);
                        popped_ += static_cast<long long>(drop);
                        fill -= drop;
                        pendingTrim_ = 0;
                        fadeIn_ = 0;
                        minFill_ = SIZE_MAX;
                        fillWindowFrames_ = 0;
                        metrics_.latencyCaps.fetch_add(1, std::memory_order_relaxed);
                    }
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
                    if (++framesSinceUnderrun_ >= kMarginRelaxFrames && marginSamples_ > kInitialMargin) {
                        // Calm for a while: give back one step of the cushion; the trim then removes the slack.
                        marginSamples_ -= std::min(kMarginStep, marginSamples_ - kInitialMargin);
                        framesSinceUnderrun_ = 0;
                    }
                } else {
                    // The worker is late. This frame is silence (not the microphone) and the isolated voice
                    // continues one frame later; keep a larger cushion from now on.
                    metrics_.underruns.fetch_add(1, std::memory_order_relaxed);
                    pendingTrim_ = 0;
                    minFill_ = SIZE_MAX;
                    fillWindowFrames_ = 0;
                    framesSinceUnderrun_ = 0;
                    // Never grow the cushion beyond what the delay limit leaves after the chunk itself.
                    const auto limit = static_cast<std::size_t>(maxLatencySamples_.load(std::memory_order_relaxed));
                    const std::size_t used = n + burstSamples48k(k);
                    const std::size_t room = limit > used + kInitialMargin ? limit - used : kInitialMargin;
                    marginSamples_ = std::min({marginSamples_ + kMarginStep, kMaxMargin, room});
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
    ScopedDenormalSuppression denormals; // the resamplers and the noise filter run on muted (zero) output too
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
            stream_->setNoiseFilter(!testFactory_); // test models are checked sample by sample
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
    // A fresh stream starts unmuted; the checker starts judging afresh, and older state resets are void.
    gateGain_ = 1.0f;
    gate_.store(kGateOpen, std::memory_order_release);
    seenStateResets_ = stateResets_.load(std::memory_order_acquire);
    checkRestarts_.fetch_add(1, std::memory_order_acq_rel);
    if (checkEvent_ != nullptr) {
        SetEvent(checkEvent_);
    }
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
                const std::uint32_t resets = stateResets_.load(std::memory_order_acquire);
                if (resets != seenStateResets_) {
                    seenStateResets_ = resets;
                    stream_->resetState(); // the voice check found someone else: forget whom the model followed
                }
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
                if (isolated > 0 && checkActive_.load(std::memory_order_relaxed)) {
                    // The checker hears the output before the gate, so it notices when the user is back.
                    checkRing_.tryPush(out16_.data(), static_cast<std::size_t>(isolated));
                    SetEvent(checkEvent_);
                }
                int gate = gate_.load(std::memory_order_acquire);
                if (gate == kGateOnSpeech && isolated > 0) {
                    // Muted until someone speaks after a pause: open on this chunk, not after the checker has
                    // seen it, so the first syllable passes. The checker then judges the new speech.
                    double energy = 0.0;
                    for (int i = 0; i < isolated; ++i) {
                        energy += static_cast<double>(out16_[static_cast<std::size_t>(i)]) * out16_[static_cast<std::size_t>(i)];
                    }
                    if (energy / isolated > speechLevel_.load(std::memory_order_relaxed) &&
                        gate_.compare_exchange_strong(gate, kGateOpen, std::memory_order_acq_rel)) {
                        gate = kGateOpen;
                    }
                }
                const float target = gate == kGateOpen ? 1.0f : 0.0f;
                if (gateGain_ != target || target == 0.0f) {
                    for (int i = 0; i < isolated; ++i) {
                        gateGain_ = target > gateGain_ ? std::min(target, gateGain_ + kGateOpenStep)
                                                       : std::max(target, gateGain_ - kGateStep);
                        out16_[static_cast<std::size_t>(i)] *= gateGain_;
                    }
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

// ---- Voice check thread ------------------------------------------------------------------------------

void VoiceIsolator::updateSpeakerReference(const std::vector<float>& recording16k) {
    std::lock_guard lock(speakerMutex_);
    speakerReference_.clear();
    if (speakerModel_ && !recording16k.empty()) {
        const auto start = std::chrono::steady_clock::now();
        speakerReference_ = voxlock::referenceEmbedding(*speakerModel_, recording16k);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (speakerReference_.empty()) {
            logWarning("Voice check: the recording has too little speech for a reference; other voices are not muted");
        } else {
            logInfo("Voice check: reference built from " + std::to_string(recording16k.size() / voxlock::kSampleRate) +
                    " s of recording in " + std::to_string(ms) + " ms");
        }
    }
    const bool active = !speakerReference_.empty();
    checkActive_.store(active, std::memory_order_release);
    {
        std::lock_guard statusLock(statusMutex_);
        status_.voiceCheck = active;
    }
    statusRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (checkEvent_ != nullptr) {
        SetEvent(checkEvent_);
    }
}

void VoiceIsolator::checkerMain() {
    setCurrentThreadName(L"VoiceIsolator voice check");
    preferEfficiencyCores(); // not real-time: keep it off the cores the isolation runs on
    std::vector<float> block(kCheckBlock);
    std::vector<float> embedding;
    while (!workerQuit_.load(std::memory_order_acquire)) {
        WaitForSingleObject(checkEvent_, 100);
        const std::uint32_t restarts = checkRestarts_.load(std::memory_order_acquire);
        if (restarts != seenCheckRestarts_) {
            seenCheckRestarts_ = restarts;
            voiceCheck_.reset();
            publishedGate_ = voxlock::VoiceCheck::Gate::Open; // the worker opened the gate
            checkRing_.skip(checkRing_.available());
            metrics_.voiceGateOpen.store(true, std::memory_order_relaxed);
        }
        if (!checkActive_.load(std::memory_order_acquire)) {
            checkRing_.skip(checkRing_.available());
            if (!voiceCheck_.open()) {
                voiceCheck_.reset();
            }
            publishedGate_ = voxlock::VoiceCheck::Gate::Open;
            gate_.store(kGateOpen, std::memory_order_release);
            metrics_.voiceGateOpen.store(true, std::memory_order_relaxed);
            continue;
        }
        for (;;) {
            const std::size_t n = checkRing_.pop(block.data(), block.size());
            if (n == 0) {
                break;
            }
            bool reset = false;
            if (voiceCheck_.feed(block.data(), n) == voxlock::VoiceCheck::Action::Check) {
                bool ok = false;
                float score = 0.0f;
                {
                    std::lock_guard lock(speakerMutex_);
                    const std::vector<float>& speech = voiceCheck_.speech();
                    if (speakerModel_ && !speakerReference_.empty() &&
                        speakerModel_->embed(speech.data(), speech.size(), embedding)) {
                        score = voxlock::similarity(embedding, speakerReference_);
                        ok = true;
                    }
                }
                if (ok) {
                    logDebug("Voice check: similarity " + std::to_string(score).substr(0, 5) + " over " +
                             std::to_string(voiceCheck_.speech().size() / 160) + "0 ms of speech");
                    const bool wasOpen = voiceCheck_.open();
                    reset = voiceCheck_.decide(score);
                    metrics_.voiceCheckScore.store(score, std::memory_order_relaxed);
                    if (wasOpen && !voiceCheck_.open()) {
                        metrics_.voiceCheckMutes.fetch_add(1, std::memory_order_relaxed);
                        logInfo("Voice check: another voice muted (similarity " + std::to_string(score).substr(0, 5) + ")");
                    } else if (!wasOpen && voiceCheck_.open()) {
                        logInfo("Voice check: your voice again (similarity " + std::to_string(score).substr(0, 5) + ")");
                    }
                }
            }
            reset = voiceCheck_.resetDue() || reset;
            speechLevel_.store(voiceCheck_.speechLevel(), std::memory_order_relaxed);
            // Only on a change: the worker may have opened an OpenOnSpeech gate by itself.
            if (voiceCheck_.gate() != publishedGate_) {
                publishedGate_ = voiceCheck_.gate();
                gate_.store(static_cast<int>(publishedGate_), std::memory_order_release);
            }
            metrics_.voiceGateOpen.store(voiceCheck_.open(), std::memory_order_relaxed);
            if (reset) {
                stateResets_.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    }
}

} // namespace vs
