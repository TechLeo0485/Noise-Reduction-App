#include "core/DspMath.h"
#include "dsp/Biquad.h"
#include "dsp/VoiceStabilizer.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace vs;

namespace {

constexpr std::size_t kFrame = 480;

// Voiced speech stand-in: a 140 Hz pulse train with harmonics up to ~3 kHz, so it has both low and mid energy.
std::vector<float> voice(double seconds, float amplitude) {
    const auto count = static_cast<std::size_t>(seconds * 48000.0);
    std::vector<float> x(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        double v = 0.0;
        for (int h = 1; h <= 20; ++h) {
            v += std::sin(test::kTwoPi * 140.0 * h * t) / h;
        }
        x[i] = static_cast<float>(amplitude * v / 2.0);
    }
    return x;
}

// Runs `x` through the stabilizer in 10 ms frames, all marked as speech; returns the output RMS (dBFS) of the last second.
float stabilizedLevelDb(VoiceStabilizer& s, std::vector<float> x, float vad = 1.0f) {
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame) {
        s.setVoiceProbability(vad);
        s.process(x.data() + off, kFrame);
    }
    return linearToDb(static_cast<float>(test::rmsOf(x, x.size() - 48000)));
}

// Proximity effect: +9 dB below ~200 Hz.
std::vector<float> closeUp(std::vector<float> x) {
    Biquad shelf;
    shelf.setCoeffs(designLowShelf(48000.0f, 200.0f, 9.0f));
    shelf.processBlock(x.data(), x.size());
    return x;
}

} // namespace

TEST(VoiceStabilizer, QuietAndLoudSpeechEndUpAtTheSameLevel) {
    // The same voice 6 dB quieter (further away) and 6 dB louder (closer) than usual.
    VoiceStabilizer far;
    far.prepare(48000.0f, kFrame);
    VoiceStabilizer near;
    near.prepare(48000.0f, kFrame);
    const float farDb = stabilizedLevelDb(far, voice(6.0, 0.05f));
    const float nearDb = stabilizedLevelDb(near, voice(6.0, 0.2f));
    EXPECT_NEAR(farDb, nearDb, 1.5f) << "12 dB apart at the microphone";
    EXPECT_NEAR(farDb, VoiceStabilizer::kTargetDb, 2.0f);
}

TEST(VoiceStabilizer, NeverPullsUpNoiseBetweenWords) {
    VoiceStabilizer s;
    s.prepare(48000.0f, kFrame);
    std::vector<float> noise = voice(4.0, 0.002f); // quiet, and the VAD says nobody speaks
    stabilizedLevelDb(s, noise, 0.0f);
    EXPECT_FLOAT_EQ(s.currentGainDb(), 0.0f);
}

TEST(VoiceStabilizer, UndoesTheProximityBoostWhenLeaningIn) {
    VoiceStabilizer s;
    s.prepare(48000.0f, kFrame);
    stabilizedLevelDb(s, voice(8.0, 0.1f));         // the usual distance: learns the balance
    EXPECT_NEAR(s.currentToneDb(), 0.0f, 0.5f);
    stabilizedLevelDb(s, closeUp(voice(6.0, 0.1f))); // now close to the microphone
    EXPECT_LT(s.currentToneDb(), -3.0f) << "the extra low end must be cut";
    EXPECT_GE(s.currentToneDb(), VoiceStabilizer::kMaxToneCutDb);
}

TEST(VoiceStabilizer, DisabledGlidesBackToUnity) {
    VoiceStabilizer s;
    s.prepare(48000.0f, kFrame);
    stabilizedLevelDb(s, voice(4.0, 0.02f));
    EXPECT_GT(s.currentGainDb(), 3.0f);
    s.configure(false);
    stabilizedLevelDb(s, voice(4.0, 0.02f));
    EXPECT_FLOAT_EQ(s.currentGainDb(), 0.0f);
    EXPECT_FLOAT_EQ(s.currentToneDb(), 0.0f);
}

TEST(VoiceStabilizer, OutputStaysFinite) {
    VoiceStabilizer s;
    s.prepare(48000.0f, kFrame);
    std::vector<float> x = voice(2.0, 0.9f);
    stabilizedLevelDb(s, x);
    std::vector<float> silence(48000, 0.0f);
    for (std::size_t off = 0; off < silence.size(); off += kFrame) {
        s.setVoiceProbability(0.0f);
        s.process(silence.data() + off, kFrame);
    }
    for (float v : silence) {
        ASSERT_TRUE(std::isfinite(v));
        ASSERT_LT(std::fabs(v), 0.01f); // only the decaying filter tail
    }
}
