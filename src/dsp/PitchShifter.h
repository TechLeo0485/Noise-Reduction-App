#pragma once

#include "dsp/Biquad.h"
#include "dsp/IAudioProcessor.h"

#include <memory>
#include <vector>

namespace vs {

// Voice character: formant-preserving pitch shift (Signalsmith Stretch) plus a little tone shaping.
//
//   Depth    (PitchSemitones)  - pitch in semitones. Formants stay where they are, so a lower voice
//                                sounds deeper instead of boomy and muffled.
//   Body     (PitchBodySt)     - moves the formants down (positive: a bigger, thicker voice) or up.
//   Presence (PitchPresenceDb) - a 3 kHz lift that keeps a deeper voice clear and forward.
//
// After the shift, a 90 Hz high-pass and an "anti-boom" low shelf (below ~150 Hz) keep a lower voice strong and
// thick instead of bassy: the shelf cuts 1 dB per semitone lower plus 0.5 dB per semitone of body (up to 7 dB).
// Latency is about 35 ms while active and zero while bypassed (disabled, or nothing to change).
// Starting waits until the shifter has valid output, then crossfades over one block; stopping crossfades back.
class PitchShifter final : public IAudioProcessor {
public:
    PitchShifter();
    ~PitchShifter() override;
    PitchShifter(const PitchShifter&) = delete;
    PitchShifter& operator=(const PitchShifter&) = delete;

    [[nodiscard]] const char* name() const noexcept override { return "Voice character"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;
    // Latency while active (latencySamples() is 0 while bypassed); for delay budgets.
    [[nodiscard]] int activeLatencySamples() const noexcept { return latency_; }

    void configure(bool enabled, float semitones, float bodySemitones = 0.0f, float presenceDb = 0.0f) noexcept;

private:
    struct Engine;

    void applySettings() noexcept;

    std::unique_ptr<Engine> engine_;
    std::vector<float> input_;
    float sampleRate_ = 48000.0f;
    int latency_ = 0;

    bool enabled_ = false;
    float semitones_ = 0.0f;
    float body_ = 0.0f;
    float presenceDb_ = 0.0f;
    bool dirty_ = true;

    bool active_ = false; // the shifter is running (warming up or audible)
    int warmup_ = 0;      // samples until the wet output is valid after a start
    float mix_ = 0.0f;    // 0 = dry, 1 = wet

    Biquad lowCut_;
    Biquad antiBoom_;
    Biquad presence_;
};

} // namespace vs
