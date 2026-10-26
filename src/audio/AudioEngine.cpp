#include "audio/AudioEngine.h"

#include "core/DspMath.h"
#include "core/RealtimeThread.h"
#include "devices/DeviceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

namespace {
constexpr std::size_t kRingCapacity = 16384; // ~340 ms at 48 kHz; far above any target fill
constexpr float kPeakDecayPerFrame = 0.95f;  // meter fall-back (~45 dB/s)
constexpr double kFrameMs = 10.0;

std::wstring endpointId(IMMDevice* device) {
    LPWSTR raw = nullptr;
    std::wstring id;
    if (device != nullptr && SUCCEEDED(device->GetId(&raw)) && raw != nullptr) {
        id = raw;
        CoTaskMemFree(raw);
    }
    return id;
}
} // namespace

AudioEngine::AudioEngine(EngineParameters& params)
    : params_(params), outputRing_(kRingCapacity), monitorRing_(kRingCapacity), voiceCheckBuffer_(kVoiceCheckMaxSamples, 0.0f) {
    outputAdapter_.ring = &outputRing_;
    outputAdapter_.metrics = &metrics_.output;
    monitorAdapter_.ring = &monitorRing_;
    monitorAdapter_.metrics = &metrics_.monitor;
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::startVoiceCheck(double seconds) noexcept {
    if (voiceCheckState() == VoiceCheckState::Recording) {
        return false;
    }
    const auto length = std::clamp<std::size_t>(static_cast<std::size_t>(seconds * VoiceProcessingChain::kSampleRate),
                                                VoiceProcessingChain::kFrameSize, kVoiceCheckMaxSamples);
    voiceCheckLength_.store(length, std::memory_order_relaxed);
    voiceCheckPosition_.store(0, std::memory_order_relaxed);
    voiceCheckState_.store(static_cast<int>(VoiceCheckState::Recording), std::memory_order_release);
    return true;
}

void AudioEngine::cancelVoiceCheck() noexcept {
    voiceCheckState_.store(static_cast<int>(VoiceCheckState::Idle), std::memory_order_release);
}

float AudioEngine::voiceCheckProgress() const noexcept {
    const std::size_t length = voiceCheckLength_.load(std::memory_order_relaxed);
    return length > 0 ? static_cast<float>(voiceCheckPosition_.load(std::memory_order_relaxed)) / static_cast<float>(length)
                      : 0.0f;
}

std::vector<float> AudioEngine::takeVoiceCheck() {
    std::vector<float> recording;
    if (voiceCheckState() == VoiceCheckState::Ready) { // acquire: the capture thread's writes are visible
        const std::size_t n = voiceCheckPosition_.load(std::memory_order_relaxed);
        recording.assign(voiceCheckBuffer_.begin(), voiceCheckBuffer_.begin() + static_cast<std::ptrdiff_t>(n));
        voiceCheckState_.store(static_cast<int>(VoiceCheckState::Idle), std::memory_order_release);
    }
    return recording;
}

bool AudioEngine::openRender(IMMDeviceEnumerator* enumerator, const std::wstring& id, StreamRole role,
                             const EngineConfig& config, WasapiRenderStream& stream, RenderAdapter& adapter,
                             SpscRingBuffer<float>& ring, RenderPathMetrics& metrics, std::wstring& resolvedId,
                             std::string& name, std::string& error, double& effectiveTargetMs) {
    ComPtr<IMMDevice> device;
    HRESULT hr = openEndpoint(enumerator, id, eRender, device);
    if (FAILED(hr)) {
        error = "Cannot open output device: " + hresultToString(hr);
        return false;
    }
    resolvedId = endpointId(device.Get());
    name = toUtf8(deviceFriendlyName(device.Get()));
    hr = stream.open(device.Get(), config.bufferMode, config.safeBufferMs, role, error);
    if (FAILED(hr)) {
        return false;
    }
    const StreamInfo& si = stream.info();

    // Capture delivers 10 ms frames in bursts and the render side drains up to one device
    // period at a time. When the two clocks drift, their phase slowly slips until a render
    // pass sees no new frame, so the fill must cover a full frame plus one period (+ margin)
    // or a fast render clock underruns periodically. Raise the user's target if needed.
    const double minimumSafeMs = kFrameMs + si.periodMs + 2.0;
    effectiveTargetMs = std::max(config.outputTargetMs, minimumSafeMs);

    AdaptiveRingReader::Config rc;
    rc.deviceRate = static_cast<double>(si.format.sampleRate);
    rc.maxDeviceFrames = si.bufferFrames;
    rc.targetMs = effectiveTargetMs;
    rc.maxExtraMs = std::min(config.maxExtraMs, 60.0); // part of the 300 ms delay budget
    adapter.reader.configure(rc);
    ring.resetUnsafe();
    metrics.reset();
    return true;
}

AudioEngine::StartResult AudioEngine::start(const EngineConfig& config, IMMDeviceEnumerator* enumerator) {
    stop();
    StartResult result;
    info_ = EngineRuntimeInfo{};
    failure_.clear();
    metrics_.resetCounters();
    outputRing_.resetUnsafe();
    monitorRing_.resetUnsafe();
    chain_.reset();
    frameFill_ = 0;
    inputPeakHold_ = outputPeakHold_ = 0.0f;
    deadlineUs_ = config.deadlineMs * 1000.0;

    // ---- Capture (required) -----------------------------------------------------------
    ComPtr<IMMDevice> inputDevice;
    const std::wstring inputId = config.inputFollowDefault ? std::wstring() : config.inputDeviceId;
    HRESULT hr = openEndpoint(enumerator, inputId, eCapture, inputDevice);
    if (FAILED(hr)) {
        result.hr = hr;
        result.error = (inputId.empty() ? "No default microphone available: " : "Selected microphone unavailable: ") +
                       hresultToString(hr);
        return result;
    }
    info_.inputDeviceId = endpointId(inputDevice.Get());
    info_.inputName = toUtf8(deviceFriendlyName(inputDevice.Get()));

    hr = capture_.open(inputDevice.Get(), config.bufferMode, config.safeBufferMs, result.error);
    if (FAILED(hr)) {
        result.hr = hr;
        result.error = "Microphone \"" + info_.inputName + "\": " + result.error;
        return result;
    }
    info_.capture = capture_.info();

    const double captureRate = static_cast<double>(info_.capture.format.sampleRate);
    captureNeedsResample_ = captureRate != static_cast<double>(VoiceProcessingChain::kSampleRate);
    if (captureNeedsResample_) {
        const std::size_t maxIn = static_cast<std::size_t>(info_.capture.bufferFrames) * 2 + 64;
        captureResampler_.configure(captureRate, VoiceProcessingChain::kSampleRate, maxIn);
        resampled_.assign(static_cast<std::size_t>(std::ceil(maxIn * VoiceProcessingChain::kSampleRate / captureRate)) + 256,
                          0.0f);
        info_.captureResampling = true;
        info_.captureResamplerMs = captureResampler_.latencyInputSamples() * 1000.0 / captureRate;
    }

    // ---- Processed output (optional: the engine still runs meters without it) ------------
    outputActive_ = false;
    if (config.outputFollowDefault || !config.outputDeviceId.empty()) {
        const std::wstring outputId = config.outputFollowDefault ? std::wstring() : config.outputDeviceId;
        if (openRender(enumerator, outputId, StreamRole::Output, config, output_, outputAdapter_, outputRing_,
                       metrics_.output, info_.outputDeviceId, info_.outputName, info_.outputError,
                       info_.outputTargetMs)) {
            outputActive_ = true;
            info_.output = output_.info();
            info_.outputResamplerMs = outputAdapter_.reader.resamplerLatencyMs();
        }
    } else {
        info_.outputError = "No output device selected";
    }

    // ---- Monitoring (optional) ------------------------------------------------------------
    monitorActive_ = false;
    if (config.monitorEnabled) {
        if (openRender(enumerator, config.monitorDeviceId, StreamRole::Monitor, config, monitor_, monitorAdapter_,
                       monitorRing_, metrics_.monitor, info_.monitorDeviceId, info_.monitorName, info_.monitorError,
                       info_.monitorTargetMs)) {
            monitorActive_ = true;
            info_.monitor = monitor_.info();
        }
    }

    // ---- Delay budget: microphone to app stays under kMaxTotalLatencyMs ----------------------
    // Worst case of every fixed part (capture period, 10 ms framing, resamplers, suppressor, voice character, jitter
    // buffer up to its reset limit, render buffer, virtual microphone); strict isolation gets what is left.
    {
        constexpr double kChainRate = VoiceProcessingChain::kSampleRate;
        double fixedMs = info_.capture.periodMs + kFrameMs + info_.captureResamplerMs +
                         chain_.maxLatencyWithoutIsolationSamples() * 1000.0 / kChainRate + kVirtualMicLatencyMs;
        if (outputActive_) {
            fixedMs += info_.outputTargetMs + std::min(config.maxExtraMs, 60.0) + info_.outputResamplerMs +
                       info_.output.bufferMs;
        }
        info_.fixedLatencyMs = fixedMs;
        chain_.isolator().setMaxLatencyMs(kMaxTotalLatencyMs - fixedMs);
        info_.isolationMaxMs = chain_.isolator().maxLatencyMs();
    }

    // ---- Start: renders first (they output silence while priming), then capture -----------
    std::string error;
    if (outputActive_ && FAILED(output_.start(&outputAdapter_, &failure_, &metrics_.output, error))) {
        info_.outputError = error;
        output_.stop();
        outputActive_ = false;
    }
    if (monitorActive_ && FAILED(monitor_.start(&monitorAdapter_, &failure_, &metrics_.monitor, error))) {
        info_.monitorError = error;
        monitor_.stop();
        monitorActive_ = false;
    }

    metrics_.lastCaptureTick.store(HighResClock::now(), std::memory_order_relaxed);
    hr = capture_.start(this, &failure_, result.error);
    if (FAILED(hr)) {
        result.hr = hr;
        stop();
        return result;
    }

    info_.captureRunning = true;
    info_.outputRunning = outputActive_;
    info_.monitorRunning = monitorActive_;
    running_ = true;
    result.ok = true;
    return result;
}

void AudioEngine::stop() noexcept {
    // Stop the producer first so render threads never see a half-written frame sequence.
    capture_.stop();
    output_.stop();
    monitor_.stop();
    outputActive_ = false;
    monitorActive_ = false;
    running_ = false;
    info_.captureRunning = info_.outputRunning = info_.monitorRunning = false;
}

// ---- Real-time capture thread ----------------------------------------------------------------

void AudioEngine::onCapturedAudio(const float* mono, std::size_t frames, bool discontinuity) noexcept {
    const std::int64_t start = HighResClock::now();
    if (discontinuity) {
        metrics_.captureDiscontinuities.fetch_add(1, std::memory_order_relaxed);
    }

    if (!captureNeedsResample_) {
        consumeInternalRate(mono, frames);
    } else {
        std::size_t offset = 0;
        while (offset < frames) {
            const std::size_t accepted = captureResampler_.write(mono + offset, frames - offset);
            offset += accepted;
            const std::size_t produced = captureResampler_.read(resampled_.data(), resampled_.size());
            consumeInternalRate(resampled_.data(), produced);
            if (accepted == 0 && produced == 0) {
                break; // cannot make progress (should not happen with the configured capacity)
            }
        }
    }

    const std::int64_t end = HighResClock::now();
    metrics_.lastCaptureTick.store(end, std::memory_order_relaxed);
    const auto callbackUs = static_cast<float>(HighResClock::ticksToMicroseconds(end - start));
    metrics_.captureCallbackUs.tryPush(&callbackUs, 1);
}

void AudioEngine::consumeInternalRate(const float* samples, std::size_t count) noexcept {
    while (count > 0) {
        const std::size_t take = std::min(count, frame_.size() - frameFill_);
        std::memcpy(frame_.data() + frameFill_, samples, take * sizeof(float));
        frameFill_ += take;
        samples += take;
        count -= take;
        if (frameFill_ == frame_.size()) {
            processFrame();
            frameFill_ = 0;
        }
    }
}

void AudioEngine::processFrame() noexcept {
    const std::int64_t start = HighResClock::now();
    FrameDiagnostics diag;
    chain_.process(frame_.data(), params_, diag);
    const auto processingUs = static_cast<float>(HighResClock::ticksToMicroseconds(HighResClock::now() - start));

    metrics_.frameProcessingUs.tryPush(&processingUs, 1);
    metrics_.framesProcessed.fetch_add(1, std::memory_order_relaxed);
    if (processingUs > deadlineUs_) {
        metrics_.deadlineMisses.fetch_add(1, std::memory_order_relaxed);
    }
    if (diag.repairedNonFinite) {
        metrics_.nonFiniteRepairs.fetch_add(1, std::memory_order_relaxed);
    }

    if (outputActive_ && !outputRing_.tryPush(frame_.data(), frame_.size())) {
        metrics_.outputOverruns.fetch_add(1, std::memory_order_relaxed);
    }
    if (monitorActive_) {
        const float gain = dbToLinear(params_.get(ParamId::MonitorGainDb));
        for (std::size_t i = 0; i < frame_.size(); ++i) {
            monitorScratch_[i] = frame_[i] * gain;
        }
        monitorRing_.tryPush(monitorScratch_.data(), monitorScratch_.size()); // monitor overflow: drop
    }

    // Voice check: copy the processed frame into the preallocated buffer (no allocation).
    if (voiceCheckState_.load(std::memory_order_acquire) == static_cast<int>(VoiceCheckState::Recording)) {
        const std::size_t length = voiceCheckLength_.load(std::memory_order_relaxed);
        const std::size_t position = voiceCheckPosition_.load(std::memory_order_relaxed);
        const std::size_t n = std::min(frame_.size(), length - std::min(position, length));
        std::memcpy(voiceCheckBuffer_.data() + position, frame_.data(), n * sizeof(float));
        voiceCheckPosition_.store(position + n, std::memory_order_relaxed);
        if (position + n >= length) {
            // Publish only if still recording (the UI may have cancelled meanwhile).
            int expected = static_cast<int>(VoiceCheckState::Recording);
            voiceCheckState_.compare_exchange_strong(expected, static_cast<int>(VoiceCheckState::Ready),
                                                     std::memory_order_release, std::memory_order_relaxed);
        }
    }

    inputPeakHold_ = std::max(diag.inputPeak, inputPeakHold_ * kPeakDecayPerFrame);
    outputPeakHold_ = std::max(diag.outputPeak, outputPeakHold_ * kPeakDecayPerFrame);
    metrics_.inputPeak.store(inputPeakHold_, std::memory_order_relaxed);
    metrics_.inputRms.store(diag.inputRms, std::memory_order_relaxed);
    metrics_.outputPeak.store(outputPeakHold_, std::memory_order_relaxed);
    metrics_.outputRms.store(diag.outputRms, std::memory_order_relaxed);
    metrics_.voiceProbability.store(diag.voiceProbability, std::memory_order_relaxed);
    metrics_.gateGainDb.store(diag.gateGainDb, std::memory_order_relaxed);
    metrics_.compressorGainDb.store(diag.compressorGainDb, std::memory_order_relaxed);
    metrics_.limiterGainDb.store(diag.limiterGainDb, std::memory_order_relaxed);
    metrics_.stabilizerGainDb.store(diag.stabilizerGainDb, std::memory_order_relaxed);
    metrics_.stabilizerToneDb.store(diag.stabilizerToneDb, std::memory_order_relaxed);
    metrics_.activeSuppressor.store(static_cast<int>(diag.activeSuppressor), std::memory_order_relaxed);
    metrics_.requestedModeUnavailable.store(diag.requestedModeUnavailable, std::memory_order_relaxed);
    metrics_.algorithmicLatencySamples.store(diag.algorithmicLatencySamples, std::memory_order_relaxed);
}

} // namespace vs
