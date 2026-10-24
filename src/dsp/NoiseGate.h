#pragma once

#include "dsp/IAudioProcessor.h"

namespace vs {

// Noise gate implemented as a downward expander with hold and a maximum attenuation range.
//
// Below the threshold the gain falls by (ratio - 1) dB per dB, limited to `range` dB, so the
// gate closes gradually rather than chopping words. A voice-activity probability (from
// RNNoise) can hold the gate open so quiet speech is never muted: "do not mute the primary
// speaker" takes priority over silence between words.
class NoiseGate final : public IAudioProcessor {
public:
    [[nodiscard]] const char* name() const noexcept override { return "Noise gate"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;

    // Voice probability for the current block in [0, 1], or a negative value if unknown.
    void setVoiceProbability(float probability) noexcept { voiceProbability_ = probability; }

    // Direct configuration (used by tests and by updateParameters()).
    void configure(bool enabled, float thresholdDb, float rangeDb, float ratio, float attackMs,
                   float holdMs, float releaseMs, bool useVad) noexcept;

    [[nodiscard]] float currentGainDb() const noexcept { return gainDb_; }

private:
    static constexpr std::size_t kSubBlock = 16;

    float sampleRate_ = 48000.0f;
    bool enabled_ = true;
    float thresholdDb_ = -50.0f;
    float rangeDb_ = 30.0f;
    float ratio_ = 4.0f;
    float holdSamples_ = 0.0f;
    bool useVad_ = true;

    float attackCoeff_ = 0.0f;   // per sub-block, dB smoothing when opening
    float releaseCoeff_ = 0.0f;  // per sub-block, dB smoothing when closing
    float detectorRelease_ = 0.0f;

    float envelope_ = 0.0f;
    float gainDb_ = 0.0f;
    float holdRemaining_ = 0.0f;
    float voiceProbability_ = -1.0f;
};

} // namespace vs
