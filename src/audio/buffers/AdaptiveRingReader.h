#pragma once

#include "audio/buffers/Resampler.h"
#include "core/SpscRingBuffer.h"

#include <cstddef>
#include <vector>

namespace vs {

struct RenderPathMetrics;

// Real-time consumer side of the processing -> render hand-off.
//
// The capture device and the render device run on independent clocks. This reader keeps the
// SPSC ring at a target fill level by adjusting the 48 kHz -> device-rate resampling ratio
// with a small PI controller (at most +/-0.5 %, inaudible). That absorbs clock drift without
// letting latency creep up over hours.
//
// Overflow/underflow policy (documented in docs/audio-pipeline.md):
//   * Backlog above target + maxExtra: drop the excess immediately (one audible skip,
//     counted as a latency reset) so delay can never grow without bound.
//   * Ring runs dry: finish the block with silence, count an underrun, and re-prime to the
//     target fill before resuming with a short fade-in.
class AdaptiveRingReader {
public:
    struct Config {
        double deviceRate = 48000.0;
        std::size_t maxDeviceFrames = 4800;
        double targetMs = 15.0;
        double maxExtraMs = 40.0;
    };

    void configure(const Config& config); // non-real-time; allocates
    void reset() noexcept;

    // Real-time: writes exactly `frames` device-rate mono samples into `out`.
    void render(SpscRingBuffer<float>& ring, float* out, std::size_t frames, RenderPathMetrics& metrics) noexcept;

    [[nodiscard]] double targetMs() const noexcept { return config_.targetMs; }
    [[nodiscard]] double resamplerLatencyMs() const noexcept;

private:
    static constexpr double kInternalRate = 48000.0;
    static constexpr double kMaxCorrection = 0.005;
    static constexpr double kProportionalGain = 0.2;  // correction per second of fill error
    static constexpr double kIntegralGain = 0.05;
    static constexpr std::size_t kFadeSamples = 96;

    Config config_{};
    Resampler resampler_;
    std::vector<float> staging_;
    double targetSamples_ = 720.0;
    double maxSamples_ = 2640.0;
    double smoothedBacklog_ = 0.0;
    double integral_ = 0.0;
    bool priming_ = true;
    float fadeGain_ = 0.0f;
};

} // namespace vs
