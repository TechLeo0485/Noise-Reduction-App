#include "core/DspMath.h"
#include "dsp/Compressor.h"
#include "dsp/Limiter.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace vs;

TEST(Compressor, StaticCurve) {
    Compressor c;
    c.prepare(48000.0f, 480);
    c.configure(true, -20.0f, 4.0f, 5.0f, 100.0f, 0.0f);
    EXPECT_FLOAT_EQ(c.gainComputerDb(-40.0f), 0.0f);           // well below threshold
    EXPECT_NEAR(c.gainComputerDb(0.0f), -15.0f, 1e-4f);         // 20 dB over at 4:1 -> -15 dB
    EXPECT_LT(c.gainComputerDb(-20.0f), 0.0f);                  // inside the soft knee
    EXPECT_GT(c.gainComputerDb(-20.0f), -1.5f);
}

TEST(Compressor, ReducesLoudSignal) {
    Compressor c;
    c.prepare(48000.0f, 480);
    c.configure(true, -30.0f, 4.0f, 1.0f, 50.0f, 0.0f);
    std::vector<float> x = test::sine(500.0, 48000.0, 48000, 0.5f);
    for (std::size_t off = 0; off < x.size(); off += 480) {
        c.process(x.data() + off, 480);
    }
    EXPECT_LT(c.currentGainReductionDb(), -10.0f);
    EXPECT_LT(test::rmsOf(x, 24000), 0.5 / std::sqrt(2.0) * 0.5);
}

TEST(Limiter, NeverExceedsCeiling) {
    Limiter l;
    l.prepare(48000.0f, 480);
    l.configure(true, -1.0f, 50.0f);
    std::vector<float> x = test::sine(200.0, 48000.0, 9600, 2.0f); // +6 dBFS
    x[1234] = 50.0f;                                               // single-sample spike
    for (std::size_t off = 0; off < x.size(); off += 480) {
        l.process(x.data() + off, 480);
    }
    const float ceiling = dbToLinear(-1.0f);
    const float peak = *std::max_element(x.begin(), x.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    EXPECT_LE(std::fabs(peak), ceiling + 1e-6f);
}

TEST(Limiter, TransparentBelowCeiling) {
    Limiter l;
    l.prepare(48000.0f, 480);
    l.configure(true, -1.0f, 50.0f);
    std::vector<float> x = test::sine(200.0, 48000.0, 4800, 0.5f);
    const std::vector<float> original = x;
    for (std::size_t off = 0; off < x.size(); off += 480) {
        l.process(x.data() + off, 480);
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        ASSERT_FLOAT_EQ(x[i], original[i]);
    }
}

TEST(DspMath, LevelHelpers) {
    const float x[] = {0.5f, -1.0f, 0.25f};
    EXPECT_FLOAT_EQ(peakAbs(x, 3), 1.0f);
    EXPECT_NEAR(rms(x, 3), std::sqrt((0.25 + 1.0 + 0.0625) / 3.0), 1e-6);
    EXPECT_NEAR(linearToDb(0.5f), -6.0206f, 1e-3f);
    EXPECT_NEAR(dbToLinear(-6.0206f), 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(linearToDb(0.0f), kMinDb);
}

TEST(DspMath, SanitizeRepairsNonFinite) {
    float x[] = {0.1f, NAN, INFINITY, -INFINITY, 0.2f};
    EXPECT_TRUE(sanitize(x, 5));
    EXPECT_FLOAT_EQ(x[1], 0.0f);
    EXPECT_FLOAT_EQ(x[2], 0.0f);
    EXPECT_FLOAT_EQ(x[3], 0.0f);
    EXPECT_FLOAT_EQ(x[4], 0.2f);
    EXPECT_FALSE(sanitize(x, 5));
}

TEST(DspMath, GainRampEndsAtTarget) {
    std::vector<float> x(480, 1.0f);
    applyGainRamp(x.data(), x.size(), 0.0f, 1.0f);
    EXPECT_NEAR(x.back(), 1.0f, 1e-5f);
    EXPECT_LT(x.front(), 0.01f);
}
