#pragma once

#include "ai/INoiseSuppressionProcessor.h"

#include <array>
#include <memory>

struct DenoiseState;

namespace vs {

// Mode 2 - RNNoise (Xiph.Org, BSD-3-Clause). Recurrent-network noise suppression operating
// natively on 480-sample frames at 48 kHz, which is exactly the engine's internal format,
// so no extra resampling or reframing is needed.
//
// The DenoiseState is allocated once in create() (non-real-time) and reused for the
// lifetime of the object. processFrame() performs no allocation.
class RnnoiseSuppressor final : public INoiseSuppressionProcessor {
public:
    // Returns nullptr if RNNoise could not be initialised (e.g. frame size mismatch).
    [[nodiscard]] static std::unique_ptr<RnnoiseSuppressor> create();

    ~RnnoiseSuppressor() override;

    [[nodiscard]] SuppressorKind kind() const noexcept override { return SuppressorKind::RNNoise; }
    [[nodiscard]] const char* name() const noexcept override { return "RNNoise"; }
    // RNNoise uses a 20 ms window with 50% overlap: each output frame corresponds to the
    // previous input frame, i.e. one frame (10 ms) of algorithmic delay.
    [[nodiscard]] int algorithmicLatencySamples() const noexcept override { return static_cast<int>(kFrameSize); }
    void reset() noexcept override;
    float processFrame(const float* in, float* out, float strength) noexcept override;

private:
    struct StateDeleter {
        void operator()(DenoiseState* state) const noexcept;
    };

    RnnoiseSuppressor() = default;

    std::unique_ptr<DenoiseState, StateDeleter> state_;
    std::array<float, kFrameSize> scaledIn_{};
    std::array<float, kFrameSize> scaledOut_{};
    std::array<float, kFrameSize> previousInput_{}; // dry path, delay-aligned with RNNoise output
};

} // namespace vs
