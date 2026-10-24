#include "dsp/Biquad.h"
#include "dsp/Equalizer.h"
#include "dsp/HighPassFilter.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <vector>

using namespace vs;

TEST(Biquad, HighPassAttenuatesBelowCutoff) {
    Biquad f;
    f.setCoeffs(designHighPass(48000.0f, 80.0f, 0.7071f));
    EXPECT_LT(f.magnitudeAt(48000.0f, 20.0f), 0.1f);
    EXPECT_NEAR(f.magnitudeAt(48000.0f, 80.0f), 0.7071f, 0.02f);
    EXPECT_NEAR(f.magnitudeAt(48000.0f, 1000.0f), 1.0f, 0.02f);
}

TEST(Biquad, PeakingGainAtCentre) {
    Biquad f;
    f.setCoeffs(designPeaking(48000.0f, 1000.0f, 1.0f, 6.0f));
    EXPECT_NEAR(test::toDb(f.magnitudeAt(48000.0f, 1000.0f)), 6.0, 0.1);
    EXPECT_NEAR(test::toDb(f.magnitudeAt(48000.0f, 50.0f)), 0.0, 0.3);
}

TEST(Biquad, ShelvesReachTheirGain) {
    Biquad low;
    low.setCoeffs(designLowShelf(48000.0f, 120.0f, 6.0f));
    EXPECT_NEAR(test::toDb(low.magnitudeAt(48000.0f, 15.0f)), 6.0, 0.3);
    EXPECT_NEAR(test::toDb(low.magnitudeAt(48000.0f, 5000.0f)), 0.0, 0.2);

    Biquad high;
    high.setCoeffs(designHighShelf(48000.0f, 7000.0f, -6.0f));
    EXPECT_NEAR(test::toDb(high.magnitudeAt(48000.0f, 20000.0f)), -6.0, 0.5);
    EXPECT_NEAR(test::toDb(high.magnitudeAt(48000.0f, 200.0f)), 0.0, 0.2);
}

TEST(Biquad, TimeDomainMatchesMagnitude) {
    Biquad f;
    f.setCoeffs(designPeaking(48000.0f, 1000.0f, 1.0f, -9.0f));
    std::vector<float> x = test::sine(1000.0, 48000.0, 48000, 0.5f);
    f.processBlock(x.data(), x.size());
    const double gainDb = test::toDb(test::rmsOf(x, 24000) / (0.5 / std::sqrt(2.0)));
    EXPECT_NEAR(gainDb, -9.0, 0.2);
}

TEST(HighPassFilter, RemovesDcOffset) {
    HighPassFilter hpf;
    hpf.prepare(48000.0f, 480);
    hpf.setCutoff(80.0f);
    std::vector<float> x(48000, 0.5f);
    hpf.process(x.data(), x.size());
    EXPECT_LT(std::fabs(x.back()), 1e-3f);
}

TEST(Equalizer, FlatWhenAllBandsZero) {
    Equalizer eq;
    eq.prepare(48000.0f, 480);
    for (float f : {50.0f, 250.0f, 1000.0f, 3500.0f, 12000.0f}) {
        EXPECT_NEAR(eq.magnitudeAt(f), 1.0f, 1e-4f);
    }
    eq.setBandGain(Equalizer::Clarity, 4.0f);
    EXPECT_NEAR(test::toDb(eq.magnitudeAt(3500.0f)), 4.0, 0.2);
}
