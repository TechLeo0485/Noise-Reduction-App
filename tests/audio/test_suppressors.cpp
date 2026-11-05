#include "ai/rnnoise/RnnoiseSuppressor.h"
#include "dsp/DspNoiseSuppressor.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <vector>

using namespace vs;

namespace {
constexpr std::size_t kFrame = INoiseSuppressionProcessor::kFrameSize;

std::vector<float> runSuppressor(INoiseSuppressionProcessor& s, const std::vector<float>& in, float strength,
                                 float* averageVad = nullptr) {
    std::vector<float> out(in.size(), 0.0f);
    double vadSum = 0.0;
    int frames = 0;
    for (std::size_t off = 0; off + kFrame <= in.size(); off += kFrame) {
        const float vad = s.processFrame(in.data() + off, out.data() + off, strength);
        vadSum += vad;
        ++frames;
    }
    if (averageVad != nullptr && frames > 0) {
        *averageVad = static_cast<float>(vadSum / frames);
    }
    return out;
}
} // namespace

TEST(DspNoiseSuppressor, AttenuatesSteadyNoise) {
    DspNoiseSuppressor s;
    const std::vector<float> noise = test::whiteNoise(48000 * 3, 0.01f); // -40 dBFS RMS
    const std::vector<float> out = runSuppressor(s, noise, 1.0f);
    const double reductionDb = test::toDb(test::rmsOf(noise, 96000) / test::rmsOf(out, 96000));
    // Steady noise sits a few dB under the expander's threshold (floor + 9 dB): about 13 dB down. (With the
    // level measured on 16-sample blocks instead of smoothed, noise peaks reopened the gain: only ~2 dB.)
    EXPECT_GT(reductionDb, 10.0);
    EXPECT_LT(reductionDb, 25.0); // bounded by the configured maximum attenuation
}

TEST(DspNoiseSuppressor, PassesSpeechLevelSignal) {
    DspNoiseSuppressor s;
    std::vector<float> in = test::whiteNoise(48000 * 2, 0.005f);
    const std::vector<float> tone = test::sine(250.0, 48000.0, 48000, 0.3f);
    in.insert(in.end(), tone.begin(), tone.end()); // loud "speech" after 2 s of noise
    const std::vector<float> out = runSuppressor(s, in, 1.0f);
    const double lossDb = test::toDb(test::rmsOf(in, 48000 * 2 + 4800) / test::rmsOf(out, 48000 * 2 + 4800));
    EXPECT_LT(lossDb, 1.0);
}

TEST(DspNoiseSuppressor, ZeroStrengthIsTransparent) {
    DspNoiseSuppressor s;
    const std::vector<float> noise = test::whiteNoise(48000, 0.01f);
    const std::vector<float> out = runSuppressor(s, noise, 0.0f);
    EXPECT_NEAR(test::rmsOf(out, 4800), test::rmsOf(noise, 4800), 1e-6);
}

TEST(RnnoiseSuppressor, SuppressesWhiteNoise) {
    auto rnnoise = RnnoiseSuppressor::create();
    if (!rnnoise) {
        GTEST_SKIP() << "Built without RNNoise";
    }
    const std::vector<float> noise = test::whiteNoise(48000 * 4, 0.03f); // ~ -30 dBFS
    float vad = 0.0f;
    const std::vector<float> out = runSuppressor(*rnnoise, noise, 1.0f, &vad);
    const double reductionDb = test::toDb(test::rmsOf(noise, 48000 * 2) / test::rmsOf(out, 48000 * 2));
    EXPECT_GT(reductionDb, 10.0) << "RNNoise reduced stationary white noise by only " << reductionDb << " dB";
    EXPECT_LT(vad, 0.5f); // noise should not be classified as voice on average
}

TEST(RnnoiseSuppressor, StrengthBlendsDelayAlignedDryPath) {
    auto rnnoise = RnnoiseSuppressor::create();
    if (!rnnoise) {
        GTEST_SKIP() << "Built without RNNoise";
    }
    const std::vector<float> noise = test::whiteNoise(48000 * 2, 0.03f);
    const std::vector<float> out = runSuppressor(*rnnoise, noise, 0.0f);
    // Strength 0 must reproduce the input exactly, delayed by one frame.
    for (std::size_t i = kFrame; i < noise.size(); ++i) {
        ASSERT_FLOAT_EQ(out[i], noise[i - kFrame]);
    }
}

TEST(RnnoiseSuppressor, ReportsOneFrameLatency) {
    auto rnnoise = RnnoiseSuppressor::create();
    if (!rnnoise) {
        GTEST_SKIP() << "Built without RNNoise";
    }
    EXPECT_EQ(rnnoise->algorithmicLatencySamples(), 480);
}
