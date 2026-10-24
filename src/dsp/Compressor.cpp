#include "dsp/Compressor.h"

#include "core/DspMath.h"
#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

void Compressor::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    configure(enabled_, thresholdDb_, ratio_, 8.0f, 150.0f, makeupDb_);
    reset();
}

void Compressor::reset() noexcept {
    envelope_ = 0.0f;
    mix_ = enabled_ ? 1.0f : 0.0f;
    lastGainReductionDb_ = 0.0f;
}

void Compressor::configure(bool enabled, float thresholdDb, float ratio, float attackMs, float releaseMs,
                           float makeupDb) noexcept {
    enabled_ = enabled;
    thresholdDb_ = thresholdDb;
    ratio_ = std::max(1.0f, ratio);
    makeupDb_ = makeupDb;
    attackCoeff_ = timeConstantCoeff(attackMs, sampleRate_);
    releaseCoeff_ = timeConstantCoeff(releaseMs, sampleRate_);
}

void Compressor::updateParameters(const EngineParameters& p) noexcept {
    configure(p.getBool(ParamId::CompressorEnabled), p.get(ParamId::CompThresholdDb), p.get(ParamId::CompRatio),
              p.get(ParamId::CompAttackMs), p.get(ParamId::CompReleaseMs), p.get(ParamId::CompMakeupDb));
}

float Compressor::gainComputerDb(float levelDb) const noexcept {
    const float over = levelDb - thresholdDb_;
    const float slope = 1.0f / ratio_ - 1.0f; // negative
    if (2.0f * over < -kKneeDb) {
        return 0.0f;
    }
    if (2.0f * std::fabs(over) <= kKneeDb) {
        const float x = over + kKneeDb * 0.5f;
        return slope * x * x / (2.0f * kKneeDb);
    }
    return slope * over;
}

void Compressor::process(float* samples, std::size_t count) noexcept {
    if (count == 0) {
        return;
    }
    const float targetMix = enabled_ ? 1.0f : 0.0f;
    const float mixStart = mix_;
    const float mixStep = (targetMix - mixStart) / static_cast<float>(count);

    if (mixStart == 0.0f && targetMix == 0.0f) {
        // Fully bypassed: keep the detector warm so re-enabling does not pump.
        for (std::size_t i = 0; i < count; ++i) {
            const float x = std::fabs(samples[i]);
            const float c = x > envelope_ ? attackCoeff_ : releaseCoeff_;
            envelope_ = c * envelope_ + (1.0f - c) * x;
        }
        lastGainReductionDb_ = 0.0f;
        return;
    }

    float mix = mixStart;
    float minGainDb = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        mix += mixStep;
        const float x = std::fabs(samples[i]);
        const float c = x > envelope_ ? attackCoeff_ : releaseCoeff_;
        envelope_ = c * envelope_ + (1.0f - c) * x;

        const float grDb = gainComputerDb(linearToDb(envelope_));
        minGainDb = std::min(minGainDb, grDb);
        samples[i] *= dbToLinear(mix * (grDb + makeupDb_));
    }
    mix_ = targetMix;
    lastGainReductionDb_ = minGainDb * targetMix;
}

} // namespace vs
