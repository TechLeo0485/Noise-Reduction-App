#pragma once

#include "dsp/Biquad.h"
#include "dsp/IAudioProcessor.h"

namespace vs {

// Keeps the voice the same when the distance to the microphone changes (leaning in, sitting back).
//
//   Level - a slow speech leveler brings speech to about kTargetDb RMS before the voice effects, so the
//           compressor and the voice character get the same level at any distance (-10..+12 dB).
//   Tone  - close to a directional microphone the low end swells (proximity effect); further away the voice
//           gets thin. The balance of low (below 250 Hz) to mid (500-2000 Hz) speech energy is compared with
//           the speaker's own long-term balance, and a 200 Hz low shelf undoes the difference (-8..+4 dB).
//
// Both adapt only while someone speaks (RNNoise voice activity, or the level above the tracked noise floor),
// hold between words and glide slowly, so background noise is never pulled up and nothing pumps.
// Zero latency. Disabling glides back to unity.
class VoiceStabilizer final : public IAudioProcessor {
public:
    static constexpr float kTargetDb = -24.0f;
    static constexpr float kMaxBoostDb = 12.0f;
    static constexpr float kMaxCutDb = -10.0f;
    static constexpr float kMaxToneBoostDb = 4.0f;
    static constexpr float kMaxToneCutDb = -8.0f;

    [[nodiscard]] const char* name() const noexcept override { return "Voice stabilizer"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;

    void configure(bool enabled) noexcept { enabled_ = enabled; }
    // Voice probability of the current block in [0, 1], or negative if unknown.
    void setVoiceProbability(float probability) noexcept { voiceProbability_ = probability; }

    [[nodiscard]] float currentGainDb() const noexcept { return gainDb_; }
    [[nodiscard]] float currentToneDb() const noexcept { return toneDb_; }

private:
    [[nodiscard]] bool isSpeech(float levelDb) noexcept;

    float sampleRate_ = 48000.0f;
    bool enabled_ = true;
    float voiceProbability_ = -1.0f;

    // Analysis (input side, not audible)
    Biquad lowBand_;
    Biquad midHigh_;
    Biquad midLow_;
    float noiseFloorDb_ = -70.0f;
    float speechPower_ = 0.0f;  // smoothed speech power (0 until speech was heard)
    float lowFast_ = 0.0f;      // band powers while speaking: ~1.5 s ...
    float midFast_ = 0.0f;
    float lowRef_ = 0.0f;       // ... and the speaker's long-term balance (~2 min)
    float midRef_ = 0.0f;
    int speechBlocks_ = 0;

    // Correction (audible)
    Biquad shelf_;
    float gainDb_ = 0.0f;
    float toneDb_ = 0.0f;
    float appliedToneDb_ = 1000.0f; // forces the first coefficient update
};

} // namespace vs
