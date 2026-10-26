#pragma once

#include "ai/INoiseSuppressionProcessor.h"
#include "audio/isolation/VoiceIsolator.h"
#include "core/Parameters.h"
#include "dsp/Compressor.h"
#include "dsp/DspNoiseSuppressor.h"
#include "dsp/Equalizer.h"
#include "dsp/HighPassFilter.h"
#include "dsp/Limiter.h"
#include "dsp/NoiseGate.h"
#include "dsp/PitchShifter.h"
#include "dsp/VoiceStabilizer.h"

#include <array>
#include <memory>

namespace vs {

class RnnoiseSuppressor;

struct FrameDiagnostics {
    float inputPeak = 0.0f;
    float inputRms = 0.0f;
    float outputPeak = 0.0f;
    float outputRms = 0.0f;
    float voiceProbability = -1.0f;
    float gateGainDb = 0.0f;
    float compressorGainDb = 0.0f;
    float limiterGainDb = 0.0f;
    float stabilizerGainDb = 0.0f; // level correction of the voice stabilizer
    float stabilizerToneDb = 0.0f; // its 200 Hz shelf (negative: closer than usual)
    int algorithmicLatencySamples = 0;
    SuppressorKind activeSuppressor = SuppressorKind::Off;
    bool requestedModeUnavailable = false;
    bool repairedNonFinite = false;
    bool isolationEnabled = false;
    bool isolationDelivering = false; // false while enabled: the frame was silenced (fail-closed)
    int isolationLatencySamples = 0;
};

// The complete per-frame voice processing pipeline (48 kHz mono, 10 ms frames):
//
//   input gain -> high-pass -> strict voice isolation (optional, VoxLock on its own thread)
//   -> noise suppressor (Off / DSP / RNNoise) -> noise gate -> voice stabilizer (distance)
//   -> [EQ, compressor, pitch in configurable order] -> voice volume -> mute
//   -> limiter -> NaN guard + hard clip
//
// Construction allocates everything (including the RNNoise state) so process() is
// real-time safe. Not thread-safe: process() must be called from one thread at a time.
class VoiceProcessingChain {
public:
    static constexpr int kSampleRate = INoiseSuppressionProcessor::kSampleRate;
    static constexpr std::size_t kFrameSize = INoiseSuppressionProcessor::kFrameSize;

    VoiceProcessingChain();
    ~VoiceProcessingChain();
    VoiceProcessingChain(const VoiceProcessingChain&) = delete;
    VoiceProcessingChain& operator=(const VoiceProcessingChain&) = delete;

    [[nodiscard]] bool isAvailable(SuppressorKind kind) const noexcept;

    // Real-time safe. Only call while process() is not running concurrently.
    void reset() noexcept;

    // Processes exactly kFrameSize samples in place.
    void process(float* frame, const EngineParameters& params, FrameDiagnostics& diag) noexcept;

    [[nodiscard]] VoiceIsolator& isolator() noexcept { return *isolator_; }

    // The most the chain adds besides strict isolation (suppressor + active voice character), for delay budgets.
    [[nodiscard]] int maxLatencyWithoutIsolationSamples() const noexcept;

private:
    [[nodiscard]] SuppressorKind resolveTarget(const EngineParameters& params, bool& unavailable) const noexcept;
    float runSuppressor(SuppressorKind kind, const float* in, float* out, float strength) noexcept;
    [[nodiscard]] IAudioProcessor* effectFor(EffectSlot slot) noexcept;
    [[nodiscard]] int suppressorLatency(SuppressorKind kind) const noexcept;

    HighPassFilter highPass_;
    DspNoiseSuppressor dspSuppressor_;
    std::unique_ptr<RnnoiseSuppressor> rnnoise_;
    std::unique_ptr<VoiceIsolator> isolator_;
    NoiseGate gate_;
    VoiceStabilizer stabilizer_;
    Equalizer equalizer_;
    Compressor compressor_;
    PitchShifter pitch_;
    Limiter limiter_;

    SuppressorKind currentSuppressor_ = SuppressorKind::Off;
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    float muteGain_ = 1.0f;

    std::array<float, kFrameSize> bufferA_{};
    std::array<float, kFrameSize> bufferB_{};
    std::array<float, kFrameSize> crossfade_{};
};

} // namespace vs
