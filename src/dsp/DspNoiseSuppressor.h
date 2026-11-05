#pragma once

#include "ai/INoiseSuppressionProcessor.h"

namespace vs {

// Mode 1 - "DSP only" noise control.
//
// A broadband downward expander whose threshold follows an estimate of the background noise
// floor (minimum tracking: falls quickly, rises slowly). Signal close to the floor is
// attenuated by up to kMaxAttenuationDb * strength; signal clearly above it passes unchanged.
//
// It reduces steady background noise in pauses, but unlike RNNoise it cannot remove noise
// that overlaps speech in time. It costs almost no CPU and adds no latency.
class DspNoiseSuppressor final : public INoiseSuppressionProcessor {
public:
    DspNoiseSuppressor() noexcept;

    [[nodiscard]] SuppressorKind kind() const noexcept override { return SuppressorKind::DspOnly; }
    [[nodiscard]] const char* name() const noexcept override { return "DSP noise control"; }
    [[nodiscard]] int algorithmicLatencySamples() const noexcept override { return 0; }
    void reset() noexcept override;
    float processFrame(const float* in, float* out, float strength) noexcept override;

    [[nodiscard]] float noiseFloorDb() const noexcept { return noiseFloorDb_; }

private:
    static constexpr std::size_t kSubBlock = 16;
    static constexpr float kMaxAttenuationDb = 24.0f;
    static constexpr float kMarginDb = 9.0f;
    static constexpr float kFloorRiseDbPerSecond = 2.5f;
    // The level is the energy smoothed over this long. The gain still changes every sub-block, but a 16-sample
    // (0.33 ms) level jumps by +-10 dB on noise alone, and the peaks kept reopening the gain: steady noise was
    // lowered by only ~2 dB instead of ~13 dB.
    static constexpr float kLevelMs = 10.0f;
    static constexpr float kBlockRate = static_cast<float>(kSampleRate) / static_cast<float>(kSubBlock);

    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    float floorFallCoeff_ = 0.0f;
    float levelCoeff_ = 0.0f;
    float energy_ = -1.0f; // smoothed mean square; negative = no block yet
    float noiseFloorDb_ = -30.0f;
    float gainDb_ = 0.0f;
};

} // namespace vs
