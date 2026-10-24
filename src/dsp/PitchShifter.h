#pragma once

#include "dsp/IAudioProcessor.h"

#include <memory>

namespace vs {

// Low-latency time-domain pitch shifter (dual modulated delay taps with sine crossfade).
//
// Chosen over phase-vocoder / PSOLA approaches because its latency is small and constant
// (about half the window, ~12 ms at the default 25 ms window). It produces some audible
// modulation on large shifts, which is why it is disabled by default and kept as an optional
// effect. Enable/disable is crossfaded over one block.
class PitchShifter final : public IAudioProcessor {
public:
    [[nodiscard]] const char* name() const noexcept override { return "Pitch"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;

    void configure(bool enabled, float semitones) noexcept;

private:
    [[nodiscard]] float readDelayed(float delaySamples) const noexcept;

    static constexpr std::size_t kBufferSize = 8192; // power of two
    static constexpr std::size_t kMask = kBufferSize - 1;

    float sampleRate_ = 48000.0f;
    float windowSamples_ = 1200.0f;
    float minDelay_ = 32.0f;
    bool enabled_ = false;
    float ratio_ = 1.0f;

    std::unique_ptr<float[]> buffer_;
    std::size_t writePos_ = 0;
    float phase_ = 0.0f;
    float mix_ = 0.0f;
};

} // namespace vs
