#pragma once

#include <complex>
#include <memory>
#include <vector>

namespace vs::voxlock {

// Removes steady background noise (fans, hum, air conditioning, road noise) from VoxLock's output while the
// user speaks. VoxLock silences the pauses well, but under the voice it passes the room noise at almost its
// full level, which RNNoise afterwards can only reduce a little. This filter works on the model's own
// 32 ms output frames, so it adds no delay:
//
//   noise floor : per frequency, the minimum of the smoothed MICROPHONE spectrum (falls at once, rises
//                 3 dB/s). Tracked on the input because the model's output hides the noise in pauses.
//   gain        : spectral subtraction of that floor from the output frame (x2 over-subtraction),
//                 smoothed across neighbouring bins, never below -18 dB, so speech stays natural.
//
// Measured with the user's own voice and fan + pink noise: noise in the gaps between words 10-20 dB lower,
// steady noise that the model passes under the voice about 11 dB lower; the voice itself changes little
// (SI-SDR against the clean voice about 1 dB lower).
class NoiseFloorFilter {
public:
    static constexpr int kSize = 512;     // VoxLock frame (kNfft)
    static constexpr int kBins = kSize / 2 + 1;

    NoiseFloorFilter();
    ~NoiseFloorFilter();
    NoiseFloorFilter(const NoiseFloorFilter&) = delete;
    NoiseFloorFilter& operator=(const NoiseFloorFilter&) = delete;

    void reset() noexcept;
    // input: the kSize microphone samples the model analysed for this frame. frame: the model's output frame
    // (before kOutputGain), filtered in place. outputGain: the gain applied to the frame later.
    void process(const float* input, float* frame, float outputGain) noexcept;

private:
    struct Fft;
    std::unique_ptr<Fft> fft_;
    std::vector<float> window_;   // hann(512, periodic), like the model's analysis
    std::vector<float> time_;
    std::vector<std::complex<float>> freq_;
    std::vector<float> smoothedIn_;
    std::vector<float> noise_;
    std::vector<float> smoothedOut_;
    std::vector<float> gain_;
    bool primed_ = false;
};

} // namespace vs::voxlock
