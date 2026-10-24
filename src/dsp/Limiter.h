#pragma once

#include "dsp/IAudioProcessor.h"

namespace vs {

// Zero-latency peak limiter: instant attack envelope, smooth release, followed by a hard
// clamp at the ceiling. A look-ahead limiter would sound cleaner on transients but adds
// latency, which this application treats as more expensive. The hard clamp guarantees the
// output never exceeds the ceiling even for single-sample peaks.
class Limiter final : public IAudioProcessor {
public:
    [[nodiscard]] const char* name() const noexcept override { return "Limiter"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;

    void configure(bool enabled, float ceilingDb, float releaseMs) noexcept;
    [[nodiscard]] float currentGainReductionDb() const noexcept { return lastGainReductionDb_; }

private:
    float sampleRate_ = 48000.0f;
    bool enabled_ = true;
    float ceiling_ = 0.891f;
    float releaseCoeff_ = 0.0f;
    float envelope_ = 0.0f;
    float lastGainReductionDb_ = 0.0f;
};

} // namespace vs
