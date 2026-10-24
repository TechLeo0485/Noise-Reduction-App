#pragma once

#include <cstddef>

namespace vs {

class EngineParameters;

// A mono, in-place, real-time audio effect.
//
// Threading contract:
//   prepare()          - non-real-time; may allocate. Called before the stream starts.
//   reset(), updateParameters(), process()
//                      - real-time safe: no allocation, no locks, no I/O, bounded time.
class IAudioProcessor {
public:
    virtual ~IAudioProcessor() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    virtual void prepare(float sampleRate, std::size_t maxBlockSize) = 0;
    virtual void reset() noexcept = 0;
    // Reads the parameters relevant to this processor. Must be cheap when nothing changed.
    virtual void updateParameters(const EngineParameters& params) noexcept = 0;
    virtual void process(float* samples, std::size_t count) noexcept = 0;
    // Algorithmic latency added by this processor, in samples at the prepared rate.
    [[nodiscard]] virtual int latencySamples() const noexcept { return 0; }
};

} // namespace vs
