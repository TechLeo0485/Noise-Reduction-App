#include "audio/buffers/AdaptiveRingReader.h"
#include "audio/monitoring/EngineMetrics.h"
#include "core/SpscRingBuffer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace vs;

namespace {

// Discrete-event simulation of the capture (48 kHz, 480-sample frames) -> render
// (device rate, fixed period) hand-off with independent clocks.
struct SimulationResult {
    RenderPathMetrics metrics;
    double finalBufferedMs = 0.0;
    double finalDriftPpm = 0.0;
};

void simulate(double deviceRate, double renderPeriodFrames, double clockSkewPpm, double seconds, double targetMs,
              SimulationResult& result) {
    SpscRingBuffer<float> ring(16384);
    AdaptiveRingReader reader;
    AdaptiveRingReader::Config cfg;
    cfg.deviceRate = deviceRate;
    cfg.maxDeviceFrames = static_cast<std::size_t>(renderPeriodFrames) + 16;
    cfg.targetMs = targetMs;
    cfg.maxExtraMs = 40.0;
    reader.configure(cfg);

    std::vector<float> frame(480, 0.1f);
    std::vector<float> out(cfg.maxDeviceFrames);
    // The render device's clock runs fast/slow relative to the capture clock.
    const double renderInterval = renderPeriodFrames / (deviceRate * (1.0 + clockSkewPpm * 1e-6));
    double nextCapture = 0.0;
    double nextRender = 0.0;
    while (nextCapture < seconds || nextRender < seconds) {
        if (nextCapture <= nextRender) {
            ring.tryPush(frame.data(), frame.size());
            nextCapture += 0.010;
        } else {
            reader.render(ring, out.data(), static_cast<std::size_t>(renderPeriodFrames), result.metrics);
            nextRender += renderInterval;
        }
    }
    result.finalBufferedMs = result.metrics.bufferedMs.load();
    result.finalDriftPpm = result.metrics.driftCorrectionPpm.load();
}

} // namespace

TEST(AdaptiveRingReader, AbsorbsClockDriftWithoutDropouts) {
    SimulationResult r;
    // 44.1 kHz render device, 10 ms period, render clock 300 ppm fast, 5 simulated minutes.
    // Target = engine minimum for a 10 ms period: frame (10) + period (10) + margin (2).
    simulate(44100.0, 441.0, 300.0, 300.0, 22.0, r);
    EXPECT_EQ(r.metrics.underruns.load(), 0u);
    EXPECT_EQ(r.metrics.latencyResets.load(), 0u);
    EXPECT_NEAR(r.finalBufferedMs, 22.0, 3.0);  // latency stays bounded at the target
    EXPECT_LT(r.finalDriftPpm, -100.0);          // correction opposes the drift
    EXPECT_GT(r.finalDriftPpm, -600.0);
}

TEST(AdaptiveRingReader, SlowRenderClockAlsoConverges) {
    SimulationResult r;
    simulate(48000.0, 144.0, -250.0, 300.0, 12.0, r); // 3 ms render period, clock 250 ppm slow
    EXPECT_EQ(r.metrics.underruns.load(), 0u);
    EXPECT_EQ(r.metrics.latencyResets.load(), 0u);
    EXPECT_NEAR(r.finalBufferedMs, 12.0, 3.0);
    EXPECT_GT(r.finalDriftPpm, 100.0);
}

TEST(AdaptiveRingReader, DiscardsExcessBacklogInsteadOfGrowingLatency) {
    SpscRingBuffer<float> ring(16384);
    AdaptiveRingReader reader;
    AdaptiveRingReader::Config cfg;
    cfg.deviceRate = 48000.0;
    cfg.maxDeviceFrames = 480;
    cfg.targetMs = 10.0;
    cfg.maxExtraMs = 40.0;
    reader.configure(cfg);
    RenderPathMetrics metrics;

    std::vector<float> backlog(14400, 0.2f); // 300 ms dumped at once (e.g. after a stall)
    ASSERT_TRUE(ring.tryPush(backlog.data(), backlog.size()));
    std::vector<float> out(480);
    reader.render(ring, out.data(), out.size(), metrics);
    EXPECT_EQ(metrics.latencyResets.load(), 1u);
    EXPECT_LE(ring.available(), 480u + 64u); // back near the 10 ms target
}

TEST(AdaptiveRingReader, UnderrunOutputsSilenceAndRecovers) {
    SpscRingBuffer<float> ring(16384);
    AdaptiveRingReader reader;
    AdaptiveRingReader::Config cfg;
    cfg.deviceRate = 48000.0;
    cfg.maxDeviceFrames = 480;
    cfg.targetMs = 10.0;
    reader.configure(cfg);
    RenderPathMetrics metrics;
    std::vector<float> out(480, 1.0f);

    // Priming: nothing buffered yet -> silence, not an underrun.
    reader.render(ring, out.data(), out.size(), metrics);
    EXPECT_EQ(metrics.underruns.load(), 0u);
    for (float v : out) {
        ASSERT_EQ(v, 0.0f);
    }

    std::vector<float> frame(960, 0.5f);
    ring.tryPush(frame.data(), frame.size()); // 20 ms
    reader.render(ring, out.data(), out.size(), metrics);
    reader.render(ring, out.data(), out.size(), metrics);
    reader.render(ring, out.data(), out.size(), metrics); // drains past empty
    EXPECT_GE(metrics.underruns.load(), 1u);
    for (float v : out) {
        ASSERT_TRUE(std::isfinite(v));
        ASSERT_LE(std::fabs(v), 0.5f * 1.05f); // allows the interpolation filter's small DC/ringing error
    }
}
