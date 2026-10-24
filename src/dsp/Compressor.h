#pragma once

#include "dsp/IAudioProcessor.h"

namespace vs {

// Feed-forward soft-knee compressor with makeup gain. Zero look-ahead (no added latency).
// Disabling fades the gain reduction and makeup out smoothly instead of switching abruptly.
class Compressor final : public IAudioProcessor {
public:
    [[nodiscard]] const char* name() const noexcept override { return "Compressor"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;

    void configure(bool enabled, float thresholdDb, float ratio, float attackMs, float releaseMs,
                   float makeupDb) noexcept;

    // Static gain curve (dB of gain change for a given input level). Exposed for tests.
    [[nodiscard]] float gainComputerDb(float levelDb) const noexcept;
    [[nodiscard]] float currentGainReductionDb() const noexcept { return lastGainReductionDb_; }

private:
    static constexpr float kKneeDb = 6.0f;

    float sampleRate_ = 48000.0f;
    bool enabled_ = true;
    float thresholdDb_ = -20.0f;
    float ratio_ = 2.5f;
    float makeupDb_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    float envelope_ = 0.0f;
    float mix_ = 1.0f; // 0 = bypassed, 1 = fully active; ramps on enable/disable
    float lastGainReductionDb_ = 0.0f;
};

} // namespace vs
