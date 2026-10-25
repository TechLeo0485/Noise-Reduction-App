#include "dsp/NoiseGate.h"

#include "core/DspMath.h"
#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

void NoiseGate::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    configure(enabled_, thresholdDb_, rangeDb_, ratio_, 2.0f, 80.0f, 150.0f, useVad_);
    reset();
}

void NoiseGate::reset() noexcept {
    envelope_ = 0.0f;
    gainDb_ = 0.0f;
    holdRemaining_ = 0.0f;
}

void NoiseGate::configure(bool enabled, float thresholdDb, float rangeDb, float ratio, float attackMs,
                          float holdMs, float releaseMs, bool useVad) noexcept {
    enabled_ = enabled;
    thresholdDb_ = thresholdDb;
    rangeDb_ = std::max(0.0f, rangeDb);
    ratio_ = std::max(1.0f, ratio);
    useVad_ = useVad;
    holdSamples_ = holdMs * 0.001f * sampleRate_;
    const float blockRate = sampleRate_ / static_cast<float>(kSubBlock);
    attackCoeff_ = timeConstantCoeff(attackMs, blockRate);
    releaseCoeff_ = timeConstantCoeff(releaseMs, blockRate);
    detectorRelease_ = timeConstantCoeff(30.0f, blockRate);
}

void NoiseGate::updateParameters(const EngineParameters& p) noexcept {
    configure(p.getBool(ParamId::GateEnabled), p.get(ParamId::GateThresholdDb), p.get(ParamId::GateRangeDb),
              p.get(ParamId::GateRatio), p.get(ParamId::GateAttackMs), p.get(ParamId::GateHoldMs),
              p.get(ParamId::GateReleaseMs), p.getBool(ParamId::GateUseVad));
}

void NoiseGate::process(float* samples, std::size_t count) noexcept {
    const bool voiceDetected = useVad_ && voiceProbability_ >= 0.5f;

    for (std::size_t offset = 0; offset < count; offset += kSubBlock) {
        const std::size_t n = std::min(kSubBlock, count - offset);
        float* block = samples + offset;

        // Peak detector: instant attack, smooth release.
        const float blockPeak = peakAbs(block, n);
        envelope_ = std::max(blockPeak, envelope_ * detectorRelease_);
        const float levelDb = linearToDb(envelope_);

        // Speech detection only protects speech that is reasonably close to the threshold. The
        // user's own voice at the microphone is loud; other people across the room are also
        // "speech" to the detector but much quieter, and must not hold the gate open.
        const bool nearFieldVoice = voiceDetected && levelDb >= thresholdDb_ - kVadMarginDb;

        float targetDb = 0.0f;
        if (!enabled_) {
            targetDb = 0.0f;
            holdRemaining_ = 0.0f;
        } else if (levelDb >= thresholdDb_ || nearFieldVoice) {
            targetDb = 0.0f;
            holdRemaining_ = holdSamples_;
        } else if (holdRemaining_ > 0.0f) {
            targetDb = 0.0f;
            holdRemaining_ -= static_cast<float>(n);
        } else {
            targetDb = std::max(-rangeDb_, (levelDb - thresholdDb_) * (ratio_ - 1.0f));
        }

        const float previousGain = dbToLinear(gainDb_);
        const float coeff = targetDb > gainDb_ ? attackCoeff_ : releaseCoeff_;
        gainDb_ = targetDb + coeff * (gainDb_ - targetDb);
        const float nextGain = dbToLinear(gainDb_);

        applyGainRamp(block, n, previousGain, nextGain);
    }
}

} // namespace vs
