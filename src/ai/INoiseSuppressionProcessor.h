#pragma once

#include <cstddef>

namespace vs {

enum class SuppressorKind : int { Off = -1, DspOnly = 0, RNNoise = 1, AdvancedAI = 2 };

[[nodiscard]] inline const char* suppressorKindName(SuppressorKind kind) noexcept {
    switch (kind) {
    case SuppressorKind::Off: return "Off";
    case SuppressorKind::DspOnly: return "DSP only";
    case SuppressorKind::RNNoise: return "RNNoise";
    case SuppressorKind::AdvancedAI: return "Advanced AI";
    }
    return "Unknown";
}

// Pluggable noise suppression engine operating on fixed 10 ms frames of 48 kHz mono audio.
//
// Implementations are constructed (and any model loaded) on a non-real-time thread.
// processFrame() is called from the real-time processing thread and must be bounded,
// allocation-free and lock-free.
class INoiseSuppressionProcessor {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr std::size_t kFrameSize = 480; // 10 ms

    virtual ~INoiseSuppressionProcessor() = default;

    [[nodiscard]] virtual SuppressorKind kind() const noexcept = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // Algorithmic delay between an input sample and its processed output, in samples.
    [[nodiscard]] virtual int algorithmicLatencySamples() const noexcept = 0;

    // Real-time safe state reset (no allocation).
    virtual void reset() noexcept = 0;

    // Processes exactly kFrameSize samples (float, nominal range [-1, 1]).
    // `strength` in [0, 1] blends between the delay-aligned input (0) and full suppression (1).
    // `in` and `out` must not alias.
    // Returns the voice-activity probability in [0, 1], or a negative value if the engine
    // does not provide one.
    virtual float processFrame(const float* in, float* out, float strength) noexcept = 0;
};

} // namespace vs
