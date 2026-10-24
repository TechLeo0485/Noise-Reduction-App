#include "dsp/NoiseGate.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace vs;

namespace {
NoiseGate makeGate(bool useVad = false) {
    NoiseGate gate;
    gate.prepare(48000.0f, 480);
    // threshold -50 dBFS, range 30 dB, ratio 4, attack 2 ms, hold 50 ms, release 100 ms
    gate.configure(true, -50.0f, 30.0f, 4.0f, 2.0f, 50.0f, 100.0f, useVad);
    return gate;
}

void run(NoiseGate& gate, std::vector<float>& x) {
    for (std::size_t off = 0; off < x.size(); off += 480) {
        gate.process(x.data() + off, std::min<std::size_t>(480, x.size() - off));
    }
}
} // namespace

TEST(NoiseGate, AttenuatesQuietSignalByRange) {
    NoiseGate gate = makeGate();
    std::vector<float> x = test::sine(300.0, 48000.0, 48000, 0.000316f); // -70 dBFS peak
    run(gate, x);
    EXPECT_NEAR(gate.currentGainDb(), -30.0f, 0.5f);
}

TEST(NoiseGate, PassesSignalAboveThreshold) {
    NoiseGate gate = makeGate();
    std::vector<float> x = test::sine(300.0, 48000.0, 48000, 0.1f); // -20 dBFS
    const double before = test::rmsOf(x, 24000);
    run(gate, x);
    EXPECT_NEAR(gate.currentGainDb(), 0.0f, 0.01f);
    EXPECT_NEAR(test::rmsOf(x, 24000), before, before * 0.001);
}

TEST(NoiseGate, VoiceActivityHoldsGateOpen) {
    NoiseGate gate = makeGate(true);
    gate.setVoiceProbability(0.9f);
    std::vector<float> x = test::sine(300.0, 48000.0, 48000, 0.000316f);
    run(gate, x);
    EXPECT_NEAR(gate.currentGainDb(), 0.0f, 0.01f);
}

TEST(NoiseGate, OpensQuicklyWhenSpeechStarts) {
    NoiseGate gate = makeGate();
    std::vector<float> quiet = test::sine(300.0, 48000.0, 24000, 0.000316f);
    run(gate, quiet);
    ASSERT_LT(gate.currentGainDb(), -25.0f);
    std::vector<float> loud = test::sine(300.0, 48000.0, 960, 0.1f); // 20 ms
    run(gate, loud);
    EXPECT_GT(gate.currentGainDb(), -1.0f);
}

TEST(NoiseGate, DisabledIsTransparent) {
    NoiseGate gate;
    gate.prepare(48000.0f, 480);
    gate.configure(false, -10.0f, 80.0f, 20.0f, 1.0f, 0.0f, 10.0f, false);
    std::vector<float> x = test::sine(300.0, 48000.0, 4800, 0.001f);
    const std::vector<float> original = x;
    run(gate, x);
    for (std::size_t i = 0; i < x.size(); ++i) {
        ASSERT_FLOAT_EQ(x[i], original[i]);
    }
}
