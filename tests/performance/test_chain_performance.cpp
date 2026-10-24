// Real-time budget check for the processing chain on the build machine. This is a sanity
// bound, not a benchmark: use tools/vs_bench for reportable numbers.

#include "audio/VoiceProcessingChain.h"
#include "audio/monitoring/StatsCollector.h"
#include "core/RealtimeThread.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

using namespace vs;

TEST(ChainPerformance, RnnoiseFullChainMeetsDeadline) {
    ScopedDenormalSuppression denormals;
    EngineParameters p;
    p.set(ParamId::SuppressionMode, 1);
    p.set(ParamId::ClarityDb, 2);
    p.set(ParamId::BassDb, 1);
    VoiceProcessingChain chain;
    FrameDiagnostics d;

    std::vector<float> input = test::whiteNoise(48000 * 30, 0.05f);
    std::vector<float> timings;
    timings.reserve(input.size() / VoiceProcessingChain::kFrameSize);
    for (std::size_t off = 0; off + VoiceProcessingChain::kFrameSize <= input.size(); off += VoiceProcessingChain::kFrameSize) {
        const auto t0 = HighResClock::now();
        chain.process(input.data() + off, p, d);
        timings.push_back(static_cast<float>(HighResClock::ticksToMicroseconds(HighResClock::now() - t0)));
    }
    const TimingStats s = StatsCollector::compute(timings);
    std::printf("[ chain ] frames %zu  avg %.1f us  p50 %.1f  p95 %.1f  p99 %.1f  max %.1f us\n", s.samples, s.average,
                s.p50, s.p95, s.p99, s.max);
    RecordProperty("p99_us", static_cast<int>(s.p99));
    // 5 ms is the engine's default per-frame deadline (half of the 10 ms frame period).
    EXPECT_LT(s.p99, 5000.0);
    EXPECT_LT(s.average, 2000.0);
}
