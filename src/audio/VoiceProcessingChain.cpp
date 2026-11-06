#include "audio/VoiceProcessingChain.h"

#include "ai/rnnoise/RnnoiseSuppressor.h"
#include "core/DspMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

VoiceProcessingChain::VoiceProcessingChain()
    : rnnoise_(RnnoiseSuppressor::create()), isolator_(std::make_unique<VoiceIsolator>()) {
    const auto rate = static_cast<float>(kSampleRate);
    highPass_.prepare(rate, kFrameSize);
    clickSuppressor_.prepare(rate, kFrameSize);
    gate_.prepare(rate, kFrameSize);
    stabilizer_.prepare(rate, kFrameSize);
    equalizer_.prepare(rate, kFrameSize);
    compressor_.prepare(rate, kFrameSize);
    pitch_.prepare(rate, kFrameSize);
    limiter_.prepare(rate, kFrameSize);

    // Raised-cosine crossfade curve used when switching suppression engines.
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kFrameSize);
        crossfade_[i] = 0.5f - 0.5f * std::cos(kPi * t);
    }
}

VoiceProcessingChain::~VoiceProcessingChain() = default;

bool VoiceProcessingChain::isAvailable(SuppressorKind kind) const noexcept {
    switch (kind) {
    case SuppressorKind::Off:
    case SuppressorKind::DspOnly: return true;
    case SuppressorKind::RNNoise: return rnnoise_ != nullptr;
    case SuppressorKind::AdvancedAI: return false; // no ONNX model integrated in this build
    }
    return false;
}

void VoiceProcessingChain::reset() noexcept {
    highPass_.reset();
    clickSuppressor_.reset();
    dspSuppressor_.reset();
    if (rnnoise_) {
        rnnoise_->reset();
    }
    isolator_->reset();
    gate_.reset();
    stabilizer_.reset();
    equalizer_.reset();
    compressor_.reset();
    pitch_.reset();
    limiter_.reset();
    currentSuppressor_ = SuppressorKind::Off;
    primed_ = false;
    inputGain_ = outputGain_ = muteGain_ = 1.0f;
}

SuppressorKind VoiceProcessingChain::resolveTarget(const EngineParameters& params, bool& unavailable) const noexcept {
    unavailable = false;
    if (!params.getBool(ParamId::SuppressionEnabled)) {
        return SuppressorKind::Off;
    }
    auto requested = static_cast<SuppressorKind>(params.getInt(ParamId::SuppressionMode));
    // Fallback order: Advanced AI -> RNNoise -> DSP only. The microphone always keeps working.
    if (requested == SuppressorKind::AdvancedAI && !isAvailable(SuppressorKind::AdvancedAI)) {
        unavailable = true;
        requested = SuppressorKind::RNNoise;
    }
    if (requested == SuppressorKind::RNNoise && !isAvailable(SuppressorKind::RNNoise)) {
        unavailable = true;
        requested = SuppressorKind::DspOnly;
    }
    return requested;
}

float VoiceProcessingChain::runSuppressor(SuppressorKind kind, const float* in, float* out, float strength) noexcept {
    switch (kind) {
    case SuppressorKind::DspOnly: return dspSuppressor_.processFrame(in, out, strength);
    case SuppressorKind::RNNoise:
        if (rnnoise_) {
            return rnnoise_->processFrame(in, out, strength);
        }
        break;
    default: break;
    }
    std::memcpy(out, in, kFrameSize * sizeof(float));
    return -1.0f;
}

int VoiceProcessingChain::suppressorLatency(SuppressorKind kind) const noexcept {
    switch (kind) {
    case SuppressorKind::DspOnly: return dspSuppressor_.algorithmicLatencySamples();
    case SuppressorKind::RNNoise: return rnnoise_ ? rnnoise_->algorithmicLatencySamples() : 0;
    default: return 0;
    }
}

int VoiceProcessingChain::maxLatencyWithoutIsolationSamples() const noexcept {
    return std::max(suppressorLatency(SuppressorKind::RNNoise), suppressorLatency(SuppressorKind::DspOnly)) +
           pitch_.activeLatencySamples() + ClickSuppressor::kLatencySamples;
}

IAudioProcessor* VoiceProcessingChain::effectFor(EffectSlot slot) noexcept {
    switch (slot) {
    case EffectSlot::Equalizer: return &equalizer_;
    case EffectSlot::Compressor: return &compressor_;
    case EffectSlot::Pitch: return &pitch_;
    default: return nullptr;
    }
}

void VoiceProcessingChain::process(float* frame, const EngineParameters& params, FrameDiagnostics& diag) noexcept {
    constexpr std::size_t n = kFrameSize;

    // 0. Reject NaN/Inf from the driver before it can poison any recursive filter state.
    const bool inputRepaired = sanitize(frame, n);

    // 1. Input gain (ramped to avoid zipper noise).
    const float inputTarget = dbToLinear(params.get(ParamId::InputGainDb));
    applyGainRamp(frame, n, inputGain_, inputTarget);
    inputGain_ = inputTarget;
    diag.inputPeak = peakAbs(frame, n);
    diag.inputRms = rms(frame, n);

    // 2. High-pass.
    highPass_.updateParameters(params);
    highPass_.process(frame, n);

    // 2a. Click remover: mouse clicks, keys and taps, before isolation and noise suppression (VoxLock keeps
    //     transients next to the voice, and the gate is held open while the user talks). 8 ms delay while on.
    clickSuppressor_.updateParameters(params);
    clickSuppressor_.process(frame, n);

    // 2b. Strict voice isolation. While enabled the frame is the isolated voice or silence, never the
    //     microphone signal (the model runs on the isolator's worker thread; this call never blocks).
    IsolationFrameInfo isolation;
    isolator_->process(frame, params.getBool(ParamId::IsolationEnabled), isolation);
    diag.isolationEnabled = isolation.enabled;
    diag.isolationDelivering = isolation.delivering;
    diag.isolationLatencySamples = isolation.enabled ? isolation.latencySamples : 0;

    // 3. Noise suppression with a one-frame crossfade whenever the engine changes.
    bool unavailable = false;
    const SuppressorKind target = resolveTarget(params, unavailable);
    const float strength = params.get(ParamId::SuppressionStrength) * 0.01f;
    float vad = -1.0f;
    if (target == currentSuppressor_) {
        primed_ = false;
        vad = runSuppressor(currentSuppressor_, frame, bufferA_.data(), strength);
        std::memcpy(frame, bufferA_.data(), n * sizeof(float));
    } else if (!primed_ || primedSuppressor_ != target) {
        // An engine that was not running still holds the audio of when it last ran: RNNoise outputs its previous
        // input frame, which would be a 10 ms piece of old speech. Reset it and feed it this frame first; its
        // output joins (crossfaded) from the next frame on.
        if (target == SuppressorKind::DspOnly) {
            dspSuppressor_.reset();
        } else if (target == SuppressorKind::RNNoise && rnnoise_) {
            rnnoise_->reset();
        }
        vad = runSuppressor(currentSuppressor_, frame, bufferA_.data(), strength);
        runSuppressor(target, frame, bufferB_.data(), strength);
        std::memcpy(frame, bufferA_.data(), n * sizeof(float));
        primedSuppressor_ = target;
        primed_ = true;
    } else {
        primed_ = false;
        const float vadOld = runSuppressor(currentSuppressor_, frame, bufferA_.data(), strength);
        const float vadNew = runSuppressor(target, frame, bufferB_.data(), strength);
        for (std::size_t i = 0; i < n; ++i) {
            frame[i] = bufferA_[i] + crossfade_[i] * (bufferB_[i] - bufferA_[i]);
        }
        vad = vadNew >= 0.0f ? vadNew : vadOld;
        currentSuppressor_ = target;
    }
    diag.voiceProbability = vad;
    diag.activeSuppressor = currentSuppressor_;
    diag.requestedModeUnavailable = unavailable;

    // 4. Noise gate (held open by voice activity when available).
    gate_.setVoiceProbability(vad);
    gate_.updateParameters(params);
    gate_.process(frame, n);
    diag.gateGainDb = gate_.currentGainDb();

    // 4b. Voice stabilizer: the same level and tone close to the microphone or further away, so the effects
    //     below always get the same voice.
    stabilizer_.setVoiceProbability(vad);
    stabilizer_.updateParameters(params);
    stabilizer_.process(frame, n);
    diag.stabilizerGainDb = stabilizer_.currentGainDb();
    diag.stabilizerToneDb = stabilizer_.currentToneDb();

    // 5. Voice effects in user-defined order.
    for (EffectSlot slot : params.effectOrder()) {
        if (IAudioProcessor* fx = effectFor(slot)) {
            fx->updateParameters(params);
            fx->process(frame, n);
        }
    }
    diag.compressorGainDb = compressor_.currentGainReductionDb();

    // 6. Voice volume and master mute, both ramped.
    const float outputTarget = dbToLinear(params.get(ParamId::VoiceVolumeDb));
    applyGainRamp(frame, n, outputGain_, outputTarget);
    outputGain_ = outputTarget;
    const float muteTarget = params.getBool(ParamId::MasterMute) ? 0.0f : 1.0f;
    applyGainRamp(frame, n, muteGain_, muteTarget);
    muteGain_ = muteTarget;

    // 7. Limiter, then unconditional protection against NaN/Inf and digital overs.
    limiter_.updateParameters(params);
    limiter_.process(frame, n);
    diag.limiterGainDb = limiter_.currentGainReductionDb();
    diag.repairedNonFinite = sanitize(frame, n) || inputRepaired;
    for (std::size_t i = 0; i < n; ++i) {
        frame[i] = std::clamp(frame[i], -1.0f, 1.0f);
    }

    diag.outputPeak = peakAbs(frame, n);
    diag.outputRms = rms(frame, n);
    diag.algorithmicLatencySamples =
        suppressorLatency(currentSuppressor_) + pitch_.latencySamples() + diag.isolationLatencySamples +
        clickSuppressor_.latencySamples();
}

} // namespace vs
