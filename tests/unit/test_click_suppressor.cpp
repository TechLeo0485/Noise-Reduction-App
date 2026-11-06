#include "dsp/ClickSuppressor.h"

#include "core/Parameters.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace vs;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kFrame = 480;
constexpr int kDelay = ClickSuppressor::kLatencySamples;

// A mouse-like click at `at`: a decaying broadband burst (~2 ms) of the given peak.
void addClick(std::vector<float>& x, std::size_t at, float peak, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, 1.0f);
    for (std::size_t i = 0; i < 960 && at + i < x.size(); ++i) {
        x[at + i] += peak * 0.4f * n(rng) * std::exp(-static_cast<float>(i) / 96.0f);
    }
}

std::vector<float> run(ClickSuppressor& c, std::vector<float> x) {
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame) {
        c.process(x.data() + off, kFrame);
    }
    return x;
}

// Energy above ~3 kHz in [begin, end) (first difference as a crude high-pass).
double highEnergy(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    double e = 0.0;
    for (std::size_t i = std::max<std::size_t>(begin, 1); i < end && i < x.size(); ++i) {
        const double d = x[i] - x[i - 1];
        e += d * d;
    }
    return e / static_cast<double>(end - begin);
}

// Real audio does not start with a jump - and a jump is a click. Test signals fade in and out over 10 ms.
std::vector<float> faded(std::vector<float> x) {
    const std::size_t n = std::min<std::size_t>(480, x.size() / 2);
    for (std::size_t i = 0; i < n; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(n);
        x[i] *= g;
        x[x.size() - 1 - i] *= g;
    }
    return x;
}

ClickSuppressor make(bool enabled = true) {
    ClickSuppressor c;
    c.setEnabled(enabled);
    c.prepare(static_cast<float>(kRate), kFrame);
    return c;
}

} // namespace

TEST(ClickSuppressor, DelaysByItsLatencyAndIsOtherwiseTransparentWithoutClicks) {
    ClickSuppressor c = make();
    EXPECT_EQ(c.latencySamples(), 384);
    // The crossover keeps the level at every frequency (only the phase turns near 2 kHz).
    for (double f : {150.0, 300.0, 1000.0, 2000.0, 4000.0, 9000.0}) {
        ClickSuppressor d = make();
        const std::vector<float> in = faded(test::sine(f, kRate, 48000, 0.3f));
        const std::vector<float> out = run(d, in);
        const double gainDb = 20.0 * std::log10(test::rmsOf(out, 4800, 48000) / test::rmsOf(in, 4800, 48000));
        EXPECT_NEAR(gainDb, 0.0, 0.05) << f << " Hz";
        EXPECT_EQ(d.clicksDetected(), 0u) << f << " Hz";
    }
    // Delay: an impulse comes out kDelay samples later (spread by the crossover's phase, peak near kDelay).
    std::vector<float> impulse(9600, 0.0f);
    impulse[2400] = 1.0f;
    ClickSuppressor e = make(false); // transparent path, then switched on
    e.setEnabled(true);
    e.reset();
    const std::vector<float> out = run(e, impulse);
    std::size_t peak = 0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        peak = std::fabs(out[i]) > std::fabs(out[peak]) ? i : peak;
    }
    EXPECT_NEAR(static_cast<double>(peak), 2400.0 + kDelay, 24.0);
}

TEST(ClickSuppressor, RemovesClicksInSilenceAndOnAVowel) {
    // Quiet room + a vowel-like tone from 0.5 s to 1.5 s; clicks in the quiet part and on the vowel.
    std::vector<float> in = test::whiteNoise(96000, 0.0005f);
    const std::vector<float> vowel = faded(test::sine(220.0, kRate, 48000, 0.15f));
    const std::vector<float> formant = faded(test::sine(880.0, kRate, 48000, 0.05f));
    for (std::size_t i = 0; i < 48000; ++i) {
        in[24000 + i] += vowel[i] + formant[i];
    }
    const std::vector<std::size_t> clicks{9600, 19200, 36000, 52800, 84000};
    for (std::size_t k = 0; k < clicks.size(); ++k) {
        addClick(in, clicks[k], 0.25f, static_cast<unsigned>(k + 1));
    }
    ClickSuppressor c = make();
    const std::vector<float> out = run(c, in);
    EXPECT_EQ(c.clicksDetected(), clicks.size());
    for (std::size_t at : clicks) {
        const double before = highEnergy(in, at, at + 480);
        const double after = highEnergy(out, at + kDelay, at + kDelay + 480);
        EXPECT_LT(10.0 * std::log10(after / before), -12.0) << "click at " << at;
    }
    // The vowel itself keeps its level between the clicks.
    EXPECT_NEAR(20.0 * std::log10(test::rmsOf(out, 40000 + kDelay, 50000 + kDelay) / test::rmsOf(in, 40000, 50000)),
                0.0, 0.1);
}

TEST(ClickSuppressor, LeavesSustainedHissAndHarmonicSpeechAlone) {
    // An "s": high-frequency noise that starts within 20 ms and lasts 200 ms, between tones.
    std::vector<float> in = faded(test::sine(180.0, kRate, 48000, 0.2f));
    std::vector<float> hiss = test::whiteNoise(9600, 0.08f, 7);
    for (std::size_t i = 0; i < hiss.size(); ++i) {
        const float rise = std::min(1.0f, static_cast<float>(i) / 960.0f);
        in[19200 + i] += rise * hiss[i];
    }
    ClickSuppressor c = make();
    const std::vector<float> out = run(c, in);
    EXPECT_EQ(c.clicksDetected(), 0u);
    const double e_in = highEnergy(in, 19200, 28800);
    const double e_out = highEnergy(out, 19200 + kDelay, 28800 + kDelay);
    EXPECT_NEAR(10.0 * std::log10(e_out / e_in), 0.0, 0.5);
}

TEST(ClickSuppressor, LeavesVoicedSpeechPulsesAlone) {
    // Voiced speech: a 125 Hz buzz whose every period starts with a sharp edge (harmonics up to 6 kHz, like vocal-fold
    // pulses), after a pause and with a pause after it. Each pulse is a short high-frequency burst - but like the ones
    // before it, so none of them is a click.
    std::vector<float> in(72000, 0.0f);
    for (std::size_t i = 0; i < 48000; ++i) {
        double v = 0.0;
        for (int k = 1; k * 125 <= 6000; ++k) {
            v += std::sin(test::kTwoPi * 125.0 * k * static_cast<double>(i) / kRate) / k;
        }
        in[12000 + i] = static_cast<float>(0.08 * v);
    }
    std::vector<float> voiced(in.begin() + 12000, in.begin() + 60000);
    voiced = faded(voiced);
    std::copy(voiced.begin(), voiced.end(), in.begin() + 12000);
    ClickSuppressor c = make();
    const std::vector<float> out = run(c, in);
    // At most the very first pulse, while the voice fades in from silence (a few ms of its top end).
    EXPECT_LE(c.clicksDetected(), 1u);
    EXPECT_NEAR(20.0 * std::log10(test::rmsOf(out, 20000 + kDelay, 50000 + kDelay) / test::rmsOf(in, 20000, 50000)),
                0.0, 0.1);
}

TEST(ClickSuppressor, OffIsTheInputWithoutDelayAndSwitchingIsSmooth) {
    std::vector<float> in = test::sine(440.0, kRate, 19200, 0.3f);
    addClick(in, 4800, 0.25f, 3);
    ClickSuppressor c = make(false);
    EXPECT_EQ(c.latencySamples(), 0);
    std::vector<float> out = in;
    for (std::size_t off = 0; off + kFrame <= out.size(); off += kFrame) {
        c.process(out.data() + off, kFrame);
    }
    for (std::size_t i = 0; i < in.size(); ++i) {
        ASSERT_EQ(out[i], in[i]);
    }
    // Switching on mid-stream: no jump larger than the signal's own sample-to-sample change.
    std::vector<float> sw = test::sine(440.0, kRate, 9600, 0.3f);
    ClickSuppressor d = make(false);
    for (std::size_t off = 0; off + kFrame <= sw.size(); off += kFrame) {
        if (off == 4800) {
            d.setEnabled(true);
        }
        d.process(sw.data() + off, kFrame);
    }
    EXPECT_EQ(d.latencySamples(), kDelay);
    const float maxStep = 0.3f * static_cast<float>(test::kTwoPi * 440.0 / kRate) * 1.05f;
    for (std::size_t i = 1; i < sw.size(); ++i) {
        ASSERT_LT(std::fabs(sw[i] - sw[i - 1]), maxStep * 1.6f) << i;
    }
}

TEST(ClickSuppressor, ReadsItsSwitchFromTheParameters) {
    EngineParameters p;
    EXPECT_FALSE(p.getBool(ParamId::ClickRemovalEnabled)); // experimental: off by default
    ClickSuppressor c = make();
    c.updateParameters(p);
    std::vector<float> x(kFrame, 0.1f);
    c.process(x.data(), kFrame);
    EXPECT_EQ(c.latencySamples(), 0);
    p.set(ParamId::ClickRemovalEnabled, 1.0f);
    c.updateParameters(p);
    c.process(x.data(), kFrame);
    EXPECT_EQ(c.latencySamples(), ClickSuppressor::kLatencySamples);
}
