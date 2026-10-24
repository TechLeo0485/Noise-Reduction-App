#pragma once

#include <cstddef>

namespace vs {

// Normalised biquad coefficients (a0 == 1).
struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

// RBJ "Audio EQ Cookbook" designs. sampleRate and frequency in Hz.
[[nodiscard]] BiquadCoeffs designHighPass(float sampleRate, float frequency, float q) noexcept;
[[nodiscard]] BiquadCoeffs designLowPass(float sampleRate, float frequency, float q) noexcept;
[[nodiscard]] BiquadCoeffs designPeaking(float sampleRate, float frequency, float q, float gainDb) noexcept;
[[nodiscard]] BiquadCoeffs designLowShelf(float sampleRate, float frequency, float gainDb) noexcept;
[[nodiscard]] BiquadCoeffs designHighShelf(float sampleRate, float frequency, float gainDb) noexcept;

// Transposed direct form II biquad. Real-time safe.
class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { z1_ = z2_ = 0.0f; }

    float process(float x) noexcept {
        const float y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

    void processBlock(float* x, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = process(x[i]);
        }
    }

    // Magnitude response at `frequency` (used by tests and UI curves; not real-time critical).
    [[nodiscard]] float magnitudeAt(float sampleRate, float frequency) const noexcept;

private:
    BiquadCoeffs c_{};
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

} // namespace vs
