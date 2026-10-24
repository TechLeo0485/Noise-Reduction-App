#pragma once

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace vs::test {

inline constexpr double kTwoPi = 6.283185307179586;

inline std::vector<float> sine(double frequency, double sampleRate, std::size_t count, float amplitude = 0.5f,
                               double phase = 0.0) {
    std::vector<float> x(count);
    for (std::size_t i = 0; i < count; ++i) {
        x[i] = amplitude * static_cast<float>(std::sin(kTwoPi * frequency * static_cast<double>(i) / sampleRate + phase));
    }
    return x;
}

inline std::vector<float> whiteNoise(std::size_t count, float rmsLevel, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(0.0f, rmsLevel);
    std::vector<float> x(count);
    for (auto& v : x) {
        v = dist(rng);
    }
    return x;
}

inline double rmsOf(const float* x, std::size_t n) {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += static_cast<double>(x[i]) * x[i];
    }
    return n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}

inline double rmsOf(const std::vector<float>& x, std::size_t begin = 0, std::size_t end = static_cast<std::size_t>(-1)) {
    end = end > x.size() ? x.size() : end;
    return begin < end ? rmsOf(x.data() + begin, end - begin) : 0.0;
}

inline double toDb(double linear) {
    return linear > 1e-12 ? 20.0 * std::log10(linear) : -240.0;
}

// Frequency estimate from positive-going zero crossings.
inline double zeroCrossingFrequency(const std::vector<float>& x, double sampleRate, std::size_t begin = 0) {
    std::size_t first = 0, last = 0, crossings = 0;
    for (std::size_t i = begin + 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (crossings == 0) {
                first = i;
            }
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2) {
        return 0.0;
    }
    return static_cast<double>(crossings - 1) * sampleRate / static_cast<double>(last - first);
}

} // namespace vs::test
