#include "audio/monitoring/EngineMetrics.h"
#include "audio/monitoring/StatsCollector.h"

#include <gtest/gtest.h>

#include <vector>

using namespace vs;

TEST(StatsCollector, NearestRankPercentiles) {
    std::vector<float> values;
    for (int i = 100; i >= 1; --i) {
        values.push_back(static_cast<float>(i));
    }
    const TimingStats s = StatsCollector::compute(values);
    EXPECT_EQ(s.samples, 100u);
    EXPECT_DOUBLE_EQ(s.p50, 50.0);
    EXPECT_DOUBLE_EQ(s.p95, 95.0);
    EXPECT_DOUBLE_EQ(s.p99, 99.0);
    EXPECT_DOUBLE_EQ(s.max, 100.0);
    EXPECT_DOUBLE_EQ(s.average, 50.5);
}

TEST(StatsCollector, EmptyIsZero) {
    const TimingStats s = StatsCollector::compute(std::vector<float>{});
    EXPECT_EQ(s.samples, 0u);
    EXPECT_EQ(s.p99, 0.0);
}

TEST(StatsCollector, DrainsRingsIntoSlidingWindow) {
    EngineMetrics metrics;
    StatsCollector collector(100);
    for (int i = 0; i < 250; ++i) {
        const float v = static_cast<float>(i);
        metrics.frameProcessingUs.tryPush(&v, 1);
    }
    collector.drain(metrics);
    const TimingStats s = collector.frameStats();
    EXPECT_EQ(s.samples, 100u);  // window keeps only the most recent 100
    EXPECT_DOUBLE_EQ(s.max, 249.0);
    EXPECT_GE(s.p50, 150.0);
    EXPECT_EQ(metrics.frameProcessingUs.available(), 0u);
}
