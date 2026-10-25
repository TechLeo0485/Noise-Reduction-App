#pragma once

#include <cstddef>
#include <vector>

namespace vs::voxlock {

// Constants of the VoxLock streaming contract (voxlock/core.py and export_onnx.py, revision a50f4e1d).
inline constexpr int kSampleRate = 16000;
inline constexpr int kHop = 128;   // 8 ms
inline constexpr int kNfft = 512;  // 32 ms analysis / synthesis frame
inline constexpr int kPad = 256;   // torch.stft centre padding (reflect)
inline constexpr int kEnrollSamples = 3 * kSampleRate; // the network takes a voice sample of exactly 3 s
inline constexpr int kLstmLayers = 6;
inline constexpr int kBands = 32;
inline constexpr int kHidden = 256;
inline constexpr std::size_t kStateSize = static_cast<std::size_t>(kLstmLayers) * kBands * kHidden;
// The network returns the caller at about -2x (trained with a scale-invariant loss); this restores the level.
inline constexpr float kOutputGain = -0.5f;
// An output sample is final once the frame starting kNfft - kHop samples later has been run: 24 ms.
inline constexpr int kAlgorithmicLatencySamples = kNfft - kHop;

// Number of input samples one chunk.onnx call consumes for `frames` frames.
[[nodiscard]] constexpr int chunkInputSamples(int frames) noexcept {
    return (frames - 1) * kHop + kNfft;
}

// One call of chunk.onnx with a fixed number of frames K:
//   wav (1, (K-1)*128 + 512), h, c (6, 32, 256)  ->  frames (1, K, 512), h_out, c_out.
// The enrollment (enroll_mag, context) is bound inside the implementation.
class IChunkModel {
public:
    virtual ~IChunkModel() = default;
    [[nodiscard]] virtual int framesPerRun() const noexcept = 0;
    // Returns false on failure; the outputs are then unspecified.
    virtual bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut,
                     float* cOut) noexcept = 0;
};

// Live stream locked to one voice: a C++ port of voxlock.core.Stream.
//
// Input is 16 kHz mono in any block size. Output is the isolated signal, sample-aligned with the input
// (output sample i belongs to input sample i) and emitted 24 ms (+ up to K-1 hops of chunking) after it.
// Only the LSTM state crosses chunk boundaries, exactly as in the reference implementation.
//
// Construction allocates; push() and reset() do not.
class Stream {
public:
    // maxPushSamples: largest block passed to one push().
    Stream(int framesPerRun, int maxPushSamples);

    void reset() noexcept;

    // Appends `count` samples (count <= maxPushSamples) and writes the isolated samples that are ready to
    // `out`, which must hold maxOutputSamples(). Returns the number written, or -1 if the model failed
    // (the stream must then be reset before reuse).
    int push(IChunkModel& model, const float* in, int count, float* out) noexcept;

    [[nodiscard]] int framesPerRun() const noexcept { return framesPerRun_; }
    [[nodiscard]] int maxOutputSamples() const noexcept { return static_cast<int>(buffer_.size()); }
    [[nodiscard]] long long samplesIn() const noexcept { return samplesIn_; }
    [[nodiscard]] long long samplesOut() const noexcept { return samplesOut_; }

private:
    int run(IChunkModel& model, float* out) noexcept;

    int framesPerRun_;
    int maxPush_;
    std::vector<float> head_;      // first samples, until the start padding can be built
    int headCount_ = 0;
    bool started_ = false;
    std::vector<float> buffer_;    // padded input not yet consumed by a frame
    int bufferCount_ = 0;
    std::vector<float> ola_;       // overlap-add accumulator (kNfft between runs)
    std::vector<float> env_;       // summed squared synthesis window
    std::vector<float> window2_;   // hann(512, periodic)^2, float32 like the reference
    std::vector<float> frames_;    // K x 512 model output
    std::vector<float> h_[2];
    std::vector<float> c_[2];
    int state_ = 0;                // which of h_/c_ holds the current LSTM state
    int dropped_ = 0;              // leading padded samples still to drop from the output
    long long samplesIn_ = 0;
    long long samplesOut_ = 0;
};

} // namespace vs::voxlock
