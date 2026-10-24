#pragma once

#include "core/Parameters.h"
#include "dsp/Biquad.h"
#include "dsp/IAudioProcessor.h"

namespace vs {

// 4th-order Butterworth high-pass (two cascaded biquads, 24 dB/octave). Removes rumble,
// desk thumps, DC offset and low-frequency HVAC energy before noise suppression.
class HighPassFilter final : public IAudioProcessor {
public:
    [[nodiscard]] const char* name() const noexcept override { return "High-pass"; }

    void prepare(float sampleRate, std::size_t /*maxBlockSize*/) override {
        sampleRate_ = sampleRate;
        cutoff_ = -1.0f;
        reset();
    }

    void reset() noexcept override {
        stage1_.reset();
        stage2_.reset();
    }

    void updateParameters(const EngineParameters& params) noexcept override {
        enabled_ = params.getBool(ParamId::HighPassEnabled);
        setCutoff(params.get(ParamId::HighPassHz));
    }

    void setCutoff(float hz) noexcept {
        if (hz == cutoff_) {
            return;
        }
        cutoff_ = hz;
        // Butterworth 4th-order pole-pair Qs.
        stage1_.setCoeffs(designHighPass(sampleRate_, hz, 0.54119610f));
        stage2_.setCoeffs(designHighPass(sampleRate_, hz, 1.30656296f));
    }

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    void process(float* samples, std::size_t count) noexcept override {
        if (!enabled_) {
            return;
        }
        stage1_.processBlock(samples, count);
        stage2_.processBlock(samples, count);
    }

private:
    float sampleRate_ = 48000.0f;
    float cutoff_ = -1.0f;
    bool enabled_ = true;
    Biquad stage1_;
    Biquad stage2_;
};

} // namespace vs
