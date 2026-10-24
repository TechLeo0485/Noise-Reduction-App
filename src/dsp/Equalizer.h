#pragma once

#include "dsp/Biquad.h"
#include "dsp/IAudioProcessor.h"

#include <array>

namespace vs {

// Five-band voice equaliser:
//   Bass    - low shelf   @ 120 Hz
//   Warmth  - peaking     @ 250 Hz, Q 0.9 (body / chest resonance)
//   Mid     - peaking     @ 1 kHz,  Q 0.8
//   Clarity - peaking     @ 3.5 kHz, Q 1.0 (presence / intelligibility)
//   Treble  - high shelf  @ 7 kHz
// Bands at 0 dB are skipped entirely. Coefficients are recomputed only when a gain changes.
class Equalizer final : public IAudioProcessor {
public:
    enum Band : std::size_t { Bass = 0, Warmth, Mid, Clarity, Treble, BandCount };

    [[nodiscard]] const char* name() const noexcept override { return "Equalizer"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;

    void setBandGain(Band band, float gainDb) noexcept;
    [[nodiscard]] float magnitudeAt(float frequency) const noexcept;

private:
    static constexpr float kBypassThresholdDb = 0.05f;

    float sampleRate_ = 48000.0f;
    std::array<Biquad, BandCount> filters_{};
    std::array<float, BandCount> gainsDb_{};
    std::array<bool, BandCount> active_{};
};

} // namespace vs
