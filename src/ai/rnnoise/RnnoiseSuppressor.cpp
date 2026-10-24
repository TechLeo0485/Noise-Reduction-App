#include "ai/rnnoise/RnnoiseSuppressor.h"

#include <algorithm>

#if defined(VOICESTUDIO_HAS_RNNOISE)
extern "C" {
#include <rnnoise.h>
}
#else
// Built without RNNoise (VOICESTUDIO_WITH_RNNOISE=OFF): create() returns nullptr and the
// processing chain falls back to DSP-only suppression. These stubs are never reached.
namespace {
int rnnoise_get_frame_size() { return 0; }
DenoiseState* rnnoise_create(void*) { return nullptr; }
void rnnoise_destroy(DenoiseState*) {}
float rnnoise_process_frame(DenoiseState*, float*, const float*) { return -1.0f; }
} // namespace
#endif

namespace vs {

namespace {
// RNNoise expects samples in 16-bit integer scale.
constexpr float kToRnnoise = 32768.0f;
constexpr float kFromRnnoise = 1.0f / 32768.0f;
} // namespace

void RnnoiseSuppressor::StateDeleter::operator()(DenoiseState* state) const noexcept {
    if (state != nullptr) {
        rnnoise_destroy(state);
    }
}

std::unique_ptr<RnnoiseSuppressor> RnnoiseSuppressor::create() {
    if (rnnoise_get_frame_size() != static_cast<int>(kFrameSize)) {
        return nullptr;
    }
    std::unique_ptr<RnnoiseSuppressor> instance(new RnnoiseSuppressor());
    instance->state_.reset(rnnoise_create(nullptr)); // nullptr = built-in model
    if (!instance->state_) {
        return nullptr;
    }
    return instance;
}

RnnoiseSuppressor::~RnnoiseSuppressor() = default;

void RnnoiseSuppressor::reset() noexcept {
    // rnnoise_init() would re-run model setup; instead the recurrent state simply re-converges
    // within a few frames. Only the dry delay line is cleared here.
    previousInput_.fill(0.0f);
}

float RnnoiseSuppressor::processFrame(const float* in, float* out, float strength) noexcept {
    strength = std::clamp(strength, 0.0f, 1.0f);

    for (std::size_t i = 0; i < kFrameSize; ++i) {
        scaledIn_[i] = in[i] * kToRnnoise;
    }
    const float vad = rnnoise_process_frame(state_.get(), scaledOut_.data(), scaledIn_.data());

    if (strength >= 1.0f) {
        for (std::size_t i = 0; i < kFrameSize; ++i) {
            out[i] = scaledOut_[i] * kFromRnnoise;
        }
    } else {
        // Blend with the input delayed by one frame so the dry and wet paths stay aligned
        // (mixing with the undelayed input would comb-filter the voice).
        const float dry = 1.0f - strength;
        for (std::size_t i = 0; i < kFrameSize; ++i) {
            out[i] = strength * scaledOut_[i] * kFromRnnoise + dry * previousInput_[i];
        }
    }
    std::copy(in, in + kFrameSize, previousInput_.begin());
    return vad;
}

} // namespace vs
