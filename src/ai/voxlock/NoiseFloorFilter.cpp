#include "ai/voxlock/NoiseFloorFilter.h"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>

namespace vs::voxlock {

namespace {
constexpr float kOverSubtraction = 2.0f;
constexpr float kFloorGain = 0.125f;      // -18 dB
constexpr float kInputSmoothing = 0.1f;   // weight of the newest frame (~80 ms: the minimum stays close to the mean)
constexpr float kOutputSmoothing = 0.5f;
constexpr float kRisePerFrame = 1.00553f; // 3 dB per second at 125 frames per second (8 ms hop)
constexpr float kMaxNoise = 1e12f;
} // namespace

struct NoiseFloorFilter::Fft {
    signalsmith::linear::RealFFT<float> fft{kSize};
};

NoiseFloorFilter::NoiseFloorFilter()
    : fft_(std::make_unique<Fft>()), window_(kSize), time_(kSize), freq_(kSize / 2), smoothedIn_(kBins), noise_(kBins),
      smoothedOut_(kBins), gain_(kBins) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    for (int i = 0; i < kSize; ++i) {
        window_[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / kSize));
    }
    reset();
}

NoiseFloorFilter::~NoiseFloorFilter() = default;

void NoiseFloorFilter::reset() noexcept {
    std::fill(smoothedIn_.begin(), smoothedIn_.end(), 0.0f);
    std::fill(noise_.begin(), noise_.end(), 0.0f);
    std::fill(smoothedOut_.begin(), smoothedOut_.end(), 0.0f);
    primed_ = false;
}

void NoiseFloorFilter::process(const float* input, float* frame, float outputGain) noexcept {
    // Power spectrum of one packed real FFT (bin 0 carries DC and Nyquist) into `power` (kBins).
    const auto spectrum = [this](float* power) {
        power[0] = freq_[0].real() * freq_[0].real();
        power[kBins - 1] = freq_[0].imag() * freq_[0].imag();
        for (int k = 1; k < kSize / 2; ++k) {
            power[k] = std::norm(freq_[static_cast<std::size_t>(k)]);
        }
    };

    // Noise floor of the microphone.
    for (int i = 0; i < kSize; ++i) {
        time_[static_cast<std::size_t>(i)] = input[i] * window_[static_cast<std::size_t>(i)];
    }
    fft_->fft.fft(time_.data(), freq_.data());
    spectrum(gain_.data()); // gain_ as scratch for the input power
    for (int k = 0; k < kBins; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const float p = gain_[i];
        smoothedIn_[i] = primed_ ? smoothedIn_[i] + kInputSmoothing * (p - smoothedIn_[i]) : p;
        noise_[i] = primed_ && smoothedIn_[i] >= noise_[i] ? std::min(noise_[i] * kRisePerFrame, kMaxNoise) : smoothedIn_[i];
    }

    // Gain for the output frame.
    fft_->fft.fft(frame, freq_.data());
    spectrum(gain_.data());
    const float scale = outputGain * outputGain;
    for (int k = 0; k < kBins; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const float o = gain_[i] * scale;
        smoothedOut_[i] = primed_ ? smoothedOut_[i] + kOutputSmoothing * (o - smoothedOut_[i]) : o;
        gain_[i] = std::max(1.0f - kOverSubtraction * noise_[i] / std::max(smoothedOut_[i], 1e-20f), kFloorGain);
    }
    primed_ = true;
    // Smooth across neighbouring bins (less "musical noise"); the edges repeat their neighbour.
    float previous = gain_[0];
    for (int k = 0; k < kBins; ++k) {
        const auto i = static_cast<std::size_t>(k);
        const float current = gain_[i];
        const float next = k + 1 < kBins ? gain_[i + 1] : current;
        gain_[i] = 0.25f * previous + 0.5f * current + 0.25f * next;
        previous = current;
    }

    freq_[0] = {freq_[0].real() * gain_[0], freq_[0].imag() * gain_[kBins - 1]};
    for (int k = 1; k < kSize / 2; ++k) {
        freq_[static_cast<std::size_t>(k)] *= gain_[static_cast<std::size_t>(k)];
    }
    fft_->fft.ifft(freq_.data(), time_.data());
    constexpr float kInverseScale = 1.0f / kSize;
    for (int i = 0; i < kSize; ++i) {
        frame[i] = time_[static_cast<std::size_t>(i)] * kInverseScale;
    }
}

} // namespace vs::voxlock
