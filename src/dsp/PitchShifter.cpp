#include "dsp/PitchShifter.h"

#include "core/DspMath.h"
#include "core/Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

void PitchShifter::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    windowSamples_ = std::min(0.025f * sampleRate, static_cast<float>(kBufferSize / 2));
    minDelay_ = 32.0f;
    buffer_ = std::make_unique<float[]>(kBufferSize);
    reset();
}

void PitchShifter::reset() noexcept {
    if (buffer_) {
        std::memset(buffer_.get(), 0, kBufferSize * sizeof(float));
    }
    writePos_ = 0;
    phase_ = 0.0f;
    mix_ = enabled_ ? 1.0f : 0.0f;
}

void PitchShifter::configure(bool enabled, float semitones) noexcept {
    enabled_ = enabled && std::fabs(semitones) > 0.01f;
    ratio_ = std::pow(2.0f, semitones / 12.0f);
}

void PitchShifter::updateParameters(const EngineParameters& p) noexcept {
    configure(p.getBool(ParamId::PitchEnabled), p.get(ParamId::PitchSemitones));
}

int PitchShifter::latencySamples() const noexcept {
    return enabled_ ? static_cast<int>(minDelay_ + windowSamples_ * 0.5f) : 0;
}

float PitchShifter::readDelayed(float delaySamples) const noexcept {
    const float readPos = static_cast<float>(writePos_) - delaySamples;
    const float floorPos = std::floor(readPos);
    const float frac = readPos - floorPos;
    const auto i0 = static_cast<std::size_t>(static_cast<long long>(floorPos)) & kMask;
    const std::size_t i1 = (i0 + 1) & kMask;
    return buffer_[i0] + frac * (buffer_[i1] - buffer_[i0]);
}

void PitchShifter::process(float* samples, std::size_t count) noexcept {
    if (!buffer_ || count == 0) {
        return;
    }
    const float targetMix = enabled_ ? 1.0f : 0.0f;
    if (mix_ == 0.0f && targetMix == 0.0f) {
        // Bypassed: keep the delay line filled so a later enable starts from real audio.
        for (std::size_t i = 0; i < count; ++i) {
            buffer_[writePos_] = samples[i];
            writePos_ = (writePos_ + 1) & kMask;
        }
        return;
    }

    // The tap delay changes by (1 - ratio) samples per output sample.
    const float phaseIncrement = (1.0f - ratio_) / windowSamples_;
    const float mixStep = (targetMix - mix_) / static_cast<float>(count);
    float mix = mix_;

    for (std::size_t i = 0; i < count; ++i) {
        buffer_[writePos_] = samples[i];

        float p2 = phase_ + 0.5f;
        if (p2 >= 1.0f) {
            p2 -= 1.0f;
        }
        const float d1 = minDelay_ + phase_ * windowSamples_;
        const float d2 = minDelay_ + p2 * windowSamples_;
        // sin(pi*p) and sin(pi*(p+0.5)) are power-complementary and reach zero exactly where
        // each tap's delay wraps, hiding the discontinuity.
        const float g1 = std::sin(kPi * phase_);
        const float g2 = std::sin(kPi * p2);
        const float wet = readDelayed(d1) * g1 + readDelayed(d2) * g2;

        mix += mixStep;
        samples[i] = samples[i] + mix * (wet - samples[i]);

        writePos_ = (writePos_ + 1) & kMask;
        phase_ += phaseIncrement;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        } else if (phase_ < 0.0f) {
            phase_ += 1.0f;
        }
    }
    mix_ = targetMix;
}

} // namespace vs
