#include "audio/buffers/Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

namespace {
constexpr double kPiD = 3.14159265358979323846;

double sinc(double x) {
    if (std::fabs(x) < 1e-9) {
        return 1.0;
    }
    return std::sin(kPiD * x) / (kPiD * x);
}

// Blackman window over [-1, 1].
double blackman(double x) {
    if (std::fabs(x) >= 1.0) {
        return 0.0;
    }
    const double t = (x + 1.0) * 0.5; // 0..1
    return 0.42 - 0.5 * std::cos(2.0 * kPiD * t) + 0.08 * std::cos(4.0 * kPiD * t);
}
} // namespace

void Resampler::configure(double inputRate, double outputRate, std::size_t maxInputBlock, int halfTaps) {
    inputRate_ = inputRate;
    outputRate_ = outputRate;
    halfTaps_ = std::max(2, halfTaps);
    taps_ = halfTaps_ * 2;
    nominalStep_ = inputRate / outputRate;
    step_ = nominalStep_;

    // Cut-off relative to the input Nyquist frequency; lowered when downsampling to prevent
    // aliasing, with a small margin for the transition band.
    const double cutoff = std::min(1.0, outputRate / inputRate) * 0.94;

    table_.assign(static_cast<std::size_t>(kPhases + 1) * taps_, 0.0f);
    for (int p = 0; p <= kPhases; ++p) {
        const double frac = static_cast<double>(p) / kPhases;
        for (int t = 0; t < taps_; ++t) {
            const double x = static_cast<double>(t - halfTaps_ + 1) - frac; // offset in input samples
            const double v = cutoff * sinc(cutoff * x) * blackman(x / halfTaps_);
            table_[static_cast<std::size_t>(p) * taps_ + t] = static_cast<float>(v);
        }
    }

    buffer_.assign(maxInputBlock + static_cast<std::size_t>(taps_) * 2 + 16, 0.0f);
    reset();
}

void Resampler::reset() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    // Prime with halfTaps zeros of history so the first output is centred on the first input.
    count_ = static_cast<std::size_t>(halfTaps_);
    position_ = static_cast<double>(halfTaps_);
    step_ = nominalStep_;
}

std::size_t Resampler::bufferedInput() const noexcept {
    const double pending = static_cast<double>(count_) - position_;
    return pending > 0.0 ? static_cast<std::size_t>(pending) : 0;
}

std::size_t Resampler::inputRequiredFor(std::size_t outputFrames) const noexcept {
    if (outputFrames == 0) {
        return 0;
    }
    const double lastPosition = position_ + static_cast<double>(outputFrames - 1) * step_;
    const auto lastIndex = static_cast<std::size_t>(std::floor(lastPosition));
    const std::size_t needed = lastIndex + static_cast<std::size_t>(halfTaps_) + 2; // +1 safety
    return needed > count_ ? needed - count_ : 0;
}

std::size_t Resampler::write(const float* input, std::size_t count) noexcept {
    const std::size_t n = std::min(count, buffer_.size() - count_);
    std::memcpy(buffer_.data() + count_, input, n * sizeof(float));
    count_ += n;
    return n;
}

std::size_t Resampler::read(float* output, std::size_t maxOutput) noexcept {
    std::size_t produced = 0;
    const float* table = table_.data();
    const auto half = static_cast<std::size_t>(halfTaps_);

    while (produced < maxOutput) {
        const double floorPos = std::floor(position_);
        const auto index = static_cast<std::size_t>(floorPos);
        if (index + half >= count_) {
            break; // not enough look-ahead input yet
        }
        const double frac = position_ - floorPos;
        const double phase = frac * kPhases;
        const auto row = static_cast<std::size_t>(phase);
        const auto rowFrac = static_cast<float>(phase - static_cast<double>(row));
        const float* k0 = table + row * static_cast<std::size_t>(taps_);
        const float* k1 = k0 + taps_;
        const float* src = buffer_.data() + (index + 1 - half);

        float acc = 0.0f;
        for (int t = 0; t < taps_; ++t) {
            const float k = k0[t] + rowFrac * (k1[t] - k0[t]);
            acc += src[t] * k;
        }
        output[produced++] = acc;
        position_ += step_;
    }

    // Discard input no longer needed as history.
    const auto oldest = static_cast<std::size_t>(std::floor(position_));
    if (oldest + 1 > half) {
        const std::size_t discard = std::min(oldest + 1 - half, count_);
        if (discard > 0) {
            std::memmove(buffer_.data(), buffer_.data() + discard, (count_ - discard) * sizeof(float));
            count_ -= discard;
            position_ -= static_cast<double>(discard);
        }
    }
    return produced;
}

} // namespace vs
