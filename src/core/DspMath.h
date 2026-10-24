#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace vs {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kMinDb = -120.0f;

[[nodiscard]] inline float dbToLinear(float db) noexcept {
    return std::pow(10.0f, db * 0.05f);
}

[[nodiscard]] inline float linearToDb(float linear) noexcept {
    return linear > 1.0e-6f ? 20.0f * std::log10(linear) : kMinDb;
}

// One-pole smoothing coefficient for a time constant given in milliseconds.
[[nodiscard]] inline float timeConstantCoeff(float ms, float sampleRate) noexcept {
    if (ms <= 0.0f) {
        return 0.0f;
    }
    return std::exp(-1.0f / (0.001f * ms * sampleRate));
}

[[nodiscard]] inline float peakAbs(const float* x, std::size_t n) noexcept {
    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        peak = std::max(peak, std::fabs(x[i]));
    }
    return peak;
}

[[nodiscard]] inline float rms(const float* x, std::size_t n) noexcept {
    if (n == 0) {
        return 0.0f;
    }
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += static_cast<double>(x[i]) * x[i];
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

// Applies a linear gain ramp from `from` to `to` across the block (click-free gain changes).
inline void applyGainRamp(float* x, std::size_t n, float from, float to) noexcept {
    if (n == 0) {
        return;
    }
    if (from == to) {
        if (from != 1.0f) {
            for (std::size_t i = 0; i < n; ++i) {
                x[i] *= from;
            }
        }
        return;
    }
    const float step = (to - from) / static_cast<float>(n);
    float g = from;
    for (std::size_t i = 0; i < n; ++i) {
        g += step;
        x[i] *= g;
    }
}

// Replaces NaN/Inf with silence. Protects downstream stages and the output device from
// a misbehaving processor. Returns true if any sample was repaired.
inline bool sanitize(float* x, std::size_t n) noexcept {
    bool repaired = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i])) {
            x[i] = 0.0f;
            repaired = true;
        }
    }
    return repaired;
}

} // namespace vs
