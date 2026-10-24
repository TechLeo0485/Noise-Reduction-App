#pragma once

#include "core/SpscRingBuffer.h"

#include <atomic>
#include <cstdint>

namespace vs {

// Counters for one render path (processed output or monitoring).
struct RenderPathMetrics {
    std::atomic<std::uint64_t> underruns{0};      // render needed audio but the ring was empty
    std::atomic<std::uint64_t> latencyResets{0};  // backlog exceeded the limit and was discarded
    std::atomic<std::uint64_t> framesRendered{0}; // device frames delivered
    std::atomic<float> bufferedMs{0.0f};          // ring + resampler backlog, smoothed
    std::atomic<float> driftCorrectionPpm{0.0f};  // current adaptive-resampling correction
    std::atomic<float> devicePaddingMs{0.0f};     // audio queued in the WASAPI render buffer

    void reset() noexcept {
        underruns = 0;
        latencyResets = 0;
        framesRendered = 0;
        bufferedMs = 0.0f;
        driftCorrectionPpm = 0.0f;
        devicePaddingMs = 0.0f;
    }
};

// Live engine measurements. Written by the real-time threads with relaxed atomics (no locks),
// read by the UI / statistics thread. Timing samples flow through SPSC rings so the consumer
// can compute percentiles without the real-time side doing any sorting.
class EngineMetrics {
public:
    EngineMetrics() : frameProcessingUs(16384), captureCallbackUs(16384) {}

    // Meters (peak values include ballistics applied on the real-time side).
    std::atomic<float> inputPeak{0.0f};
    std::atomic<float> inputRms{0.0f};
    std::atomic<float> outputPeak{0.0f};
    std::atomic<float> outputRms{0.0f};
    std::atomic<float> voiceProbability{-1.0f};
    std::atomic<float> gateGainDb{0.0f};
    std::atomic<float> compressorGainDb{0.0f};
    std::atomic<float> limiterGainDb{0.0f};
    std::atomic<int> activeSuppressor{-1};
    std::atomic<bool> requestedModeUnavailable{false};
    std::atomic<int> algorithmicLatencySamples{0};

    // Capture / processing counters.
    std::atomic<std::uint64_t> framesProcessed{0};
    std::atomic<std::uint64_t> captureDiscontinuities{0}; // device reported a capture glitch
    std::atomic<std::uint64_t> outputOverruns{0};         // output ring full; frame dropped
    std::atomic<std::uint64_t> deadlineMisses{0};         // frame processing exceeded its budget
    std::atomic<std::uint64_t> nonFiniteRepairs{0};
    std::atomic<std::int64_t> lastCaptureTick{0};         // QPC tick of the last capture packet

    RenderPathMetrics output;
    RenderPathMetrics monitor;

    // Timing samples in microseconds (producer: capture thread, consumer: StatsCollector).
    SpscRingBuffer<float> frameProcessingUs; // one sample per 10 ms frame (chain only)
    SpscRingBuffer<float> captureCallbackUs; // one sample per capture wake-up (all capture work)

    // Not thread-safe with respect to running streams; call while the engine is stopped.
    void resetCounters() noexcept {
        framesProcessed = 0;
        captureDiscontinuities = 0;
        outputOverruns = 0;
        deadlineMisses = 0;
        nonFiniteRepairs = 0;
        lastCaptureTick = 0;
        output.reset();
        monitor.reset();
    }
};

} // namespace vs
