#include "dsp/Limiter.h"

#include "core/DspMath.h"
#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

void Limiter::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    configure(enabled_, linearToDb(ceiling_), 60.0f);
    reset();
}

void Limiter::reset() noexcept {
    envelope_ = 0.0f;
    lastGainReductionDb_ = 0.0f;
}

void Limiter::configure(bool enabled, float ceilingDb, float releaseMs) noexcept {
    enabled_ = enabled;
    ceiling_ = dbToLinear(std::min(0.0f, ceilingDb));
    releaseCoeff_ = timeConstantCoeff(releaseMs, sampleRate_);
}

void Limiter::updateParameters(const EngineParameters& p) noexcept {
    configure(p.getBool(ParamId::LimiterEnabled), p.get(ParamId::LimiterCeilingDb), p.get(ParamId::LimiterReleaseMs));
}

void Limiter::process(float* samples, std::size_t count) noexcept {
    if (!enabled_) {
        lastGainReductionDb_ = 0.0f;
        return;
    }
    float minGain = 1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float a = std::fabs(samples[i]);
        envelope_ = std::max(a, envelope_ * releaseCoeff_);
        const float gain = envelope_ > ceiling_ ? ceiling_ / envelope_ : 1.0f;
        minGain = std::min(minGain, gain);
        samples[i] = std::clamp(samples[i] * gain, -ceiling_, ceiling_);
    }
    lastGainReductionDb_ = linearToDb(minGain);
}

} // namespace vs
