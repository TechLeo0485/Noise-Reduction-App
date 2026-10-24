#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace vs {

// Streaming mono resampler using a windowed-sinc kernel with a precomputed polyphase table
// and linear interpolation between phases. Supports arbitrary (and slowly varying) ratios,
// which lets the render path also absorb clock drift between independent devices.
//
// Usage: write() input as it arrives, read() as much output as is available.
// configure() allocates; everything else is real-time safe.
class Resampler {
public:
    // halfTaps: kernel half-length in input samples (latency = halfTaps input samples).
    void configure(double inputRate, double outputRate, std::size_t maxInputBlock, int halfTaps = 16);
    void reset() noexcept;

    // Multiplies the nominal input-per-output step. > 1 consumes input faster (drains a
    // backlog), < 1 slower. Intended for small drift corrections (e.g. 0.995 .. 1.005).
    void setStepScale(double scale) noexcept { step_ = nominalStep_ * scale; }

    [[nodiscard]] double inputRate() const noexcept { return inputRate_; }
    [[nodiscard]] double outputRate() const noexcept { return outputRate_; }
    [[nodiscard]] bool isConfigured() const noexcept { return !buffer_.empty(); }
    [[nodiscard]] std::size_t bufferedInput() const noexcept;
    [[nodiscard]] std::size_t freeInputSpace() const noexcept { return buffer_.size() - count_; }
    [[nodiscard]] int latencyInputSamples() const noexcept { return halfTaps_; }

    // Additional input samples needed before read() can produce `outputFrames` samples.
    [[nodiscard]] std::size_t inputRequiredFor(std::size_t outputFrames) const noexcept;

    // Appends input; returns the number of samples accepted (limited by free space).
    std::size_t write(const float* input, std::size_t count) noexcept;

    // Produces up to `maxOutput` samples. Returns the number produced.
    std::size_t read(float* output, std::size_t maxOutput) noexcept;

private:
    double inputRate_ = 48000.0;
    double outputRate_ = 48000.0;
    double nominalStep_ = 1.0;
    double step_ = 1.0;
    int halfTaps_ = 16;
    int taps_ = 32;
    static constexpr int kPhases = 256;

    std::vector<float> table_;  // (kPhases + 1) rows x taps_ columns
    std::vector<float> buffer_; // linear input history + pending input
    std::size_t count_ = 0;     // valid samples in buffer_
    double position_ = 0.0;     // fractional read position into buffer_
};

} // namespace vs
