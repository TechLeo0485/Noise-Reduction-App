#include "audio/VoiceProcessingChain.h"

#include "ai/rnnoise/RnnoiseSuppressor.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace vs;

namespace {
constexpr std::size_t kFrame = VoiceProcessingChain::kFrameSize;

// Plain passthrough: everything that colours or gates the signal is off.
void makeTransparent(EngineParameters& p) {
    p.resetToDefaults();
    p.set(ParamId::SuppressionEnabled, 0);
    p.set(ParamId::HighPassEnabled, 0);
    p.set(ParamId::GateEnabled, 0);
    p.set(ParamId::CompressorEnabled, 0);
    p.set(ParamId::LimiterEnabled, 0);
    p.set(ParamId::StabilizerEnabled, 0);
    p.set(ParamId::ClickRemovalEnabled, 0);
}

std::vector<float> processAll(VoiceProcessingChain& chain, EngineParameters& p, std::vector<float> x) {
    FrameDiagnostics d;
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame) {
        chain.process(x.data() + off, p, d);
    }
    return x;
}
} // namespace

TEST(ProcessingChain, SilenceInSilenceOut) {
    for (int mode : {0, 1}) {
        EngineParameters p;
        p.set(ParamId::SuppressionMode, static_cast<float>(mode));
        VoiceProcessingChain chain;
        const std::vector<float> out = processAll(chain, p, std::vector<float>(48000, 0.0f));
        for (float v : out) {
            ASSERT_LT(std::fabs(v), 1e-6f) << "mode " << mode;
        }
    }
}

TEST(ProcessingChain, TransparentConfigurationIsBitExact) {
    EngineParameters p;
    makeTransparent(p);
    VoiceProcessingChain chain;
    const std::vector<float> in = test::sine(440.0, 48000.0, 9600, 0.4f);
    const std::vector<float> out = processAll(chain, p, in);
    // The first frame carries the compressor's disable fade (it starts enabled by default).
    for (std::size_t i = kFrame; i < in.size(); ++i) {
        ASSERT_FLOAT_EQ(out[i], in[i]);
    }
}

TEST(ProcessingChain, InputGainIsApplied) {
    EngineParameters p;
    makeTransparent(p);
    p.set(ParamId::InputGainDb, 6.0206f); // x2
    VoiceProcessingChain chain;
    const std::vector<float> in = test::sine(440.0, 48000.0, 9600, 0.2f);
    const std::vector<float> out = processAll(chain, p, in);
    for (std::size_t i = kFrame; i < in.size(); ++i) { // first frame ramps
        ASSERT_NEAR(out[i], 2.0f * in[i], 1e-4f);
    }
}

TEST(ProcessingChain, MuteSilencesAfterRamp) {
    EngineParameters p;
    p.set(ParamId::MasterMute, 1);
    VoiceProcessingChain chain;
    const std::vector<float> out = processAll(chain, p, test::sine(440.0, 48000.0, 4800, 0.5f));
    for (std::size_t i = kFrame; i < out.size(); ++i) {
        ASSERT_EQ(out[i], 0.0f);
    }
}

TEST(ProcessingChain, OutputNeverClipsEvenWithExtremeGain) {
    EngineParameters p;
    makeTransparent(p);
    p.set(ParamId::InputGainDb, 24.0f);
    p.set(ParamId::VoiceVolumeDb, 12.0f);
    VoiceProcessingChain chain;
    const std::vector<float> out = processAll(chain, p, test::sine(200.0, 48000.0, 9600, 0.9f));
    for (float v : out) {
        ASSERT_LE(std::fabs(v), 1.0f);
    }
}

TEST(ProcessingChain, RecoversFromNonFiniteInput) {
    EngineParameters p; // defaults: HPF, gate and compressor (recursive state) active
    p.set(ParamId::SuppressionEnabled, 0); // a pure tone may legitimately be treated as noise
    VoiceProcessingChain chain;
    FrameDiagnostics d;
    std::vector<float> frame(kFrame, NAN);
    chain.process(frame.data(), p, d);
    EXPECT_TRUE(d.repairedNonFinite);

    std::vector<float> speechLike = test::sine(300.0, 48000.0, 48000, 0.3f);
    for (std::size_t off = 0; off + kFrame <= speechLike.size(); off += kFrame) {
        chain.process(speechLike.data() + off, p, d);
    }
    for (float v : speechLike) {
        ASSERT_TRUE(std::isfinite(v));
    }
    // Filter state was not poisoned: real audio still comes out.
    EXPECT_GT(test::rmsOf(speechLike, 24000), 0.01);
}

TEST(ProcessingChain, ModeSwitchingIsClickFree) {
    EngineParameters p;
    makeTransparent(p);
    p.set(ParamId::SuppressionEnabled, 1);
    VoiceProcessingChain chain;
    FrameDiagnostics d;
    std::vector<float> x = test::sine(200.0, 48000.0, 48000 * 3, 0.3f);

    float previous = 0.0f;
    float worstJump = 0.0f;
    int frameIndex = 0;
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame, ++frameIndex) {
        // Cycle Off -> DSP -> RNNoise every 50 ms.
        const int phase = (frameIndex / 5) % 3;
        p.set(ParamId::SuppressionEnabled, phase == 0 ? 0.0f : 1.0f);
        p.set(ParamId::SuppressionMode, phase == 2 ? 1.0f : 0.0f);
        chain.process(x.data() + off, p, d);
        for (std::size_t i = 0; i < kFrame; ++i) {
            const float v = x[off + i];
            ASSERT_TRUE(std::isfinite(v));
            if (frameIndex > 10) {
                worstJump = std::max(worstJump, std::fabs(v - previous));
            }
            previous = v;
        }
    }
    // A 200 Hz, 0.3 amplitude sine changes by at most ~0.008 per sample; a hard switch between
    // misaligned signals would jump by up to 0.6. Crossfading keeps steps small.
    EXPECT_LT(worstJump, 0.15f);
}

TEST(ProcessingChain, SwitchingBackToRnnoisePlaysNoOldAudio) {
    if (!RnnoiseSuppressor::create()) {
        GTEST_SKIP() << "Built without RNNoise";
    }
    EngineParameters p;
    makeTransparent(p);
    p.set(ParamId::SuppressionEnabled, 1);
    p.set(ParamId::SuppressionMode, static_cast<float>(SuppressionMode::RNNoise));
    p.set(ParamId::SuppressionStrength, 0.0f); // RNNoise's output = its delayed dry input: easy to look for
    VoiceProcessingChain chain;
    // Loud audio while RNNoise runs, then 1 s of silence with suppression off, then RNNoise again.
    std::vector<float> loud = test::sine(1000.0, 48000.0, 48000 / 2, 0.5f);
    processAll(chain, p, loud);
    p.set(ParamId::SuppressionEnabled, 0);
    std::vector<float> silence(48000, 0.0f);
    processAll(chain, p, silence);
    p.set(ParamId::SuppressionEnabled, 1);
    const std::vector<float> out = processAll(chain, p, std::vector<float>(kFrame * 4, 0.0f));
    // Only silence went in since the switch; the last frame RNNoise saw half a second earlier must not come out.
    EXPECT_LT(test::rmsOf(out, 0), 1e-6);
}

TEST(ProcessingChain, UnavailableModeFallsBack) {
    EngineParameters p;
    p.set(ParamId::SuppressionMode, static_cast<float>(SuppressionMode::AdvancedAI));
    VoiceProcessingChain chain;
    FrameDiagnostics d;
    std::vector<float> frame(kFrame, 0.0f);
    chain.process(frame.data(), p, d); // the fallback engine gets its first frame ...
    chain.process(frame.data(), p, d); // ... and takes over on the second
    EXPECT_TRUE(d.requestedModeUnavailable);
    EXPECT_NE(d.activeSuppressor, SuppressorKind::AdvancedAI);
    EXPECT_NE(d.activeSuppressor, SuppressorKind::Off);
}

TEST(ProcessingChain, EffectOrderIsHonoured) {
    // Pitch before or after a strong EQ cut yields different output; both must stay finite.
    EngineParameters a;
    EngineParameters b;
    for (EngineParameters* p : {&a, &b}) {
        makeTransparent(*p);
        p->set(ParamId::PitchEnabled, 1);
        p->set(ParamId::PitchSemitones, 5);
        p->set(ParamId::CompressorEnabled, 1);
        p->set(ParamId::CompThresholdDb, -40);
    }
    b.setEffectOrder({EffectSlot::Pitch, EffectSlot::Compressor, EffectSlot::Equalizer});
    VoiceProcessingChain chainA;
    VoiceProcessingChain chainB;
    const std::vector<float> in = test::sine(300.0, 48000.0, 24000, 0.5f);
    const std::vector<float> outA = processAll(chainA, a, in);
    const std::vector<float> outB = processAll(chainB, b, in);
    double diff = 0.0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        ASSERT_TRUE(std::isfinite(outA[i]) && std::isfinite(outB[i]));
        diff += std::fabs(outA[i] - outB[i]);
    }
    EXPECT_GT(diff, 1.0);
}
