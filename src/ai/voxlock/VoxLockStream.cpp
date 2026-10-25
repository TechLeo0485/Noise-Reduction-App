#include "ai/voxlock/VoxLockStream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs::voxlock {

Stream::Stream(int framesPerRun, int maxPushSamples)
    : framesPerRun_(std::max(1, framesPerRun)), maxPush_(std::max(1, maxPushSamples)) {
    head_.assign(static_cast<std::size_t>(kPad + 1 + maxPush_), 0.0f);
    // Worst case before a run: one sample short of a chunk, plus the start padding, plus a full push.
    buffer_.assign(static_cast<std::size_t>(chunkInputSamples(framesPerRun_) + kPad + 1 + kPad + maxPush_), 0.0f);
    const auto olaSize = static_cast<std::size_t>(kNfft + framesPerRun_ * kHop);
    ola_.assign(olaSize, 0.0f);
    env_.assign(olaSize, 0.0f);
    window2_.resize(kNfft);
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    for (int i = 0; i < kNfft; ++i) {
        // WIN2 = (0.5 - 0.5 cos(2 pi n / N)).astype(float32) ** 2
        const auto w = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / kNfft));
        window2_[static_cast<std::size_t>(i)] = w * w;
    }
    frames_.assign(static_cast<std::size_t>(framesPerRun_) * kNfft, 0.0f);
    for (int i = 0; i < 2; ++i) {
        h_[i].assign(kStateSize, 0.0f);
        c_[i].assign(kStateSize, 0.0f);
    }
}

void Stream::reset() noexcept {
    headCount_ = 0;
    started_ = false;
    bufferCount_ = 0;
    std::fill(ola_.begin(), ola_.end(), 0.0f);
    std::fill(env_.begin(), env_.end(), 0.0f);
    for (int i = 0; i < 2; ++i) {
        std::fill(h_[i].begin(), h_[i].end(), 0.0f);
        std::fill(c_[i].begin(), c_[i].end(), 0.0f);
    }
    state_ = 0;
    dropped_ = 0;
    samplesIn_ = samplesOut_ = 0;
}

int Stream::push(IChunkModel& model, const float* in, int count, float* out) noexcept {
    count = std::clamp(count, 0, maxPush_);
    samplesIn_ += count;
    if (!started_) {
        std::memcpy(head_.data() + headCount_, in, static_cast<std::size_t>(count) * sizeof(float));
        headCount_ += count;
        if (headCount_ <= kPad) {
            return 0;
        }
        // buf = concat(head[1 : PAD + 1][::-1], head)  (torch.stft centre padding, reflect)
        for (int i = 0; i < kPad; ++i) {
            buffer_[static_cast<std::size_t>(i)] = head_[static_cast<std::size_t>(kPad - i)];
        }
        std::memcpy(buffer_.data() + kPad, head_.data(), static_cast<std::size_t>(headCount_) * sizeof(float));
        bufferCount_ = kPad + headCount_;
        headCount_ = 0;
        dropped_ = kPad;
        started_ = true;
    } else {
        std::memcpy(buffer_.data() + bufferCount_, in, static_cast<std::size_t>(count) * sizeof(float));
        bufferCount_ += count;
    }
    return run(model, out);
}

int Stream::run(IChunkModel& model, float* out) noexcept {
    const int k = framesPerRun_;
    const int need = chunkInputSamples(k);
    const int done = k * kHop; // samples no later frame will touch
    int written = 0;
    while (bufferCount_ >= need) {
        float* hIn = h_[state_].data();
        float* cIn = c_[state_].data();
        float* hOut = h_[state_ ^ 1].data();
        float* cOut = c_[state_ ^ 1].data();
        if (!model.run(buffer_.data(), hIn, cIn, frames_.data(), hOut, cOut)) {
            return -1;
        }
        state_ ^= 1;

        // Overlap-add the K windowed frames at a 128-sample hop, accumulating the squared window.
        for (int j = 0; j < k; ++j) {
            float* o = ola_.data() + static_cast<std::size_t>(j) * kHop;
            float* e = env_.data() + static_cast<std::size_t>(j) * kHop;
            const float* f = frames_.data() + static_cast<std::size_t>(j) * kNfft;
            for (int i = 0; i < kNfft; ++i) {
                o[i] += f[i];
                e[i] += window2_[static_cast<std::size_t>(i)];
            }
        }
        const int cut = std::min(dropped_, done);
        dropped_ -= cut;
        for (int i = cut; i < done; ++i) {
            const float e = env_[static_cast<std::size_t>(i)];
            const float y = ola_[static_cast<std::size_t>(i)] / (e > 1e-11f ? e : 1.0f);
            out[written++] = y * kOutputGain;
        }
        // Keep the tail that later frames still add to; the freed end starts at zero.
        const std::size_t tail = ola_.size() - static_cast<std::size_t>(done);
        std::memmove(ola_.data(), ola_.data() + done, tail * sizeof(float));
        std::memmove(env_.data(), env_.data() + done, tail * sizeof(float));
        std::fill(ola_.begin() + static_cast<std::ptrdiff_t>(tail), ola_.end(), 0.0f);
        std::fill(env_.begin() + static_cast<std::ptrdiff_t>(tail), env_.end(), 0.0f);

        bufferCount_ -= done;
        std::memmove(buffer_.data(), buffer_.data() + done, static_cast<std::size_t>(bufferCount_) * sizeof(float));
    }
    samplesOut_ += written;
    return written;
}

} // namespace vs::voxlock
