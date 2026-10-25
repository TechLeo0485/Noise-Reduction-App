#include "dsp/PitchShifter.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace vs;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kBlock = 480;

// A steady vowel: harmonics of f0 shaped by two formants (700 Hz and 1200 Hz).
std::vector<float> vowel(double f0, std::size_t count) {
    std::vector<float> x(count, 0.0f);
    for (int k = 1; k * f0 < 6000.0; ++k) {
        const double f = k * f0;
        const double env = 1.0 / (1.0 + std::pow((f - 700.0) / 120.0, 2)) +
                           0.6 / (1.0 + std::pow((f - 1200.0) / 150.0, 2)) + 0.02;
        for (std::size_t i = 0; i < count; ++i) {
            x[i] += static_cast<float>(0.08 * env * std::sin(test::kTwoPi * f * static_cast<double>(i) / kRate + k));
        }
    }
    return x;
}

std::vector<float> run(PitchShifter& shifter, std::vector<float> x) {
    for (std::size_t i = 0; i + kBlock <= x.size(); i += kBlock) {
        shifter.process(x.data() + i, kBlock);
    }
    return x;
}

// Fundamental by autocorrelation (80..300 Hz).
double fundamental(const std::vector<float>& x, std::size_t start, std::size_t length) {
    double best = -1.0;
    int bestLag = 0;
    for (int lag = 160; lag <= 600; ++lag) {
        double acc = 0.0;
        for (std::size_t i = start; i < start + length; ++i) {
            acc += static_cast<double>(x[i]) * x[i + static_cast<std::size_t>(lag)];
        }
        if (acc > best) {
            best = acc;
            bestLag = lag;
        }
    }
    return kRate / bestLag;
}

// Spectral centroid between 200 Hz and 4 kHz (Hann-windowed DFT): where the formant energy sits.
double centroid(const std::vector<float>& x, std::size_t start, std::size_t length) {
    double num = 0.0;
    double den = 0.0;
    for (double f = 200.0; f <= 4000.0; f += 10.0) {
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < length; ++i) {
            const double w = 0.5 - 0.5 * std::cos(test::kTwoPi * static_cast<double>(i) / static_cast<double>(length));
            const double ph = test::kTwoPi * f * static_cast<double>(i) / kRate;
            re += w * x[start + i] * std::cos(ph);
            im -= w * x[start + i] * std::sin(ph);
        }
        const double mag = re * re + im * im;
        num += f * mag;
        den += mag;
    }
    return num / den;
}

// A chest voice: like vowel(), but with the strong fundamental and low harmonics of a real voice
// (source roll-off of about 6 dB per octave).
std::vector<float> chestVowel(double f0, std::size_t count) {
    std::vector<float> x(count, 0.0f);
    for (int k = 1; k * f0 < 6000.0; ++k) {
        const double f = k * f0;
        const double env = 1.0 / (1.0 + std::pow((f - 700.0) / 120.0, 2)) +
                           0.6 / (1.0 + std::pow((f - 1200.0) / 150.0, 2)) + 0.3;
        for (std::size_t i = 0; i < count; ++i) {
            x[i] += static_cast<float>(0.08 * env / k * std::sin(test::kTwoPi * f * static_cast<double>(i) / kRate + k));
        }
    }
    return x;
}

// Energy between lo and hi Hz (Hann-windowed DFT, 5 Hz steps).
double bandEnergy(const std::vector<float>& x, std::size_t start, std::size_t length, double lo, double hi) {
    double sum = 0.0;
    for (double f = lo; f <= hi; f += 5.0) {
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < length; ++i) {
            const double w = 0.5 - 0.5 * std::cos(test::kTwoPi * static_cast<double>(i) / static_cast<double>(length));
            const double ph = test::kTwoPi * f * static_cast<double>(i) / kRate;
            re += w * x[start + i] * std::cos(ph);
            im -= w * x[start + i] * std::sin(ph);
        }
        sum += re * re + im * im;
    }
    return sum;
}

} // namespace

TEST(PitchShifter, BypassedIsTransparentWithoutLatency) {
    PitchShifter shifter;
    shifter.prepare(static_cast<float>(kRate), kBlock);
    shifter.configure(false, -4.0f, 2.0f, 3.0f);
    const std::vector<float> in = vowel(150.0, 9600);
    const std::vector<float> out = run(shifter, in);
    EXPECT_EQ(out, in);
    EXPECT_EQ(shifter.latencySamples(), 0);

    shifter.configure(true, 0.0f, 0.0f, 0.0f); // enabled, but nothing to change
    EXPECT_EQ(run(shifter, in), in);
}

TEST(PitchShifter, LowersPitchButKeepsFormants) {
    PitchShifter shifter;
    shifter.prepare(static_cast<float>(kRate), kBlock);
    shifter.configure(true, -3.0f);
    const std::vector<float> in = vowel(150.0, 96000);
    const std::vector<float> out = run(shifter, in);
    EXPECT_GT(shifter.latencySamples(), 0);
    EXPECT_LT(shifter.latencySamples(), static_cast<int>(0.05 * kRate)); // under 50 ms

    for (float v : out) {
        ASSERT_TRUE(std::isfinite(v));
    }
    const double f0 = fundamental(out, 48000, 8192);
    EXPECT_NEAR(f0, 150.0 * std::pow(2.0, -3.0 / 12.0), 4.0);

    // Plain resampling would move the formants down by 16% too (the "boomy" sound). They must stay.
    const double before = centroid(in, 48000, 8192);
    const double after = centroid(out, 48000, 8192);
    EXPECT_NEAR(after / before, 1.0, 0.07) << "centroid " << before << " -> " << after;
}

TEST(PitchShifter, BodyMovesFormantsDown) {
    PitchShifter plain;
    PitchShifter thick;
    plain.prepare(static_cast<float>(kRate), kBlock);
    thick.prepare(static_cast<float>(kRate), kBlock);
    plain.configure(true, -3.0f, 0.0f);
    thick.configure(true, -3.0f, 3.0f);
    const std::vector<float> in = vowel(150.0, 96000);
    const double c0 = centroid(run(plain, in), 48000, 8192);
    const double c1 = centroid(run(thick, in), 48000, 8192);
    EXPECT_LT(c1, c0 * 0.95) << c0 << " vs " << c1;
}

TEST(PitchShifter, ToggleIsClickFreeAndFinite) {
    PitchShifter shifter;
    shifter.prepare(static_cast<float>(kRate), kBlock);
    std::vector<float> x = vowel(150.0, 48000);
    float prev = 0.0f;
    float maxJump = 0.0f;
    for (std::size_t i = 0; i + kBlock <= x.size(); i += kBlock) {
        shifter.configure((i / 9600) % 2 == 1, -4.0f, 1.5f, 4.0f); // toggles every 200 ms
        shifter.process(x.data() + i, kBlock);
        for (std::size_t j = i; j < i + kBlock; ++j) {
            ASSERT_TRUE(std::isfinite(x[j]));
            maxJump = std::max(maxJump, std::fabs(x[j] - prev));
            prev = x[j];
        }
    }
    EXPECT_LT(maxJump, 0.5f);
}

TEST(PitchShifter, DeeperVoiceDoesNotGetBoomy) {
    // Lowering a 150 Hz voice puts its fundamental under 150 Hz. The share of energy down there (bass) must not
    // grow compared with the original voice: deeper and thicker, not boomier.
    PitchShifter shifter;
    shifter.prepare(static_cast<float>(kRate), kBlock);
    shifter.configure(true, -3.0f, 1.5f, 3.0f);
    const std::vector<float> in = chestVowel(150.0, 96000);
    const std::vector<float> out = run(shifter, in);
    const auto bassShare = [](const std::vector<float>& x) {
        return bandEnergy(x, 48000, 8192, 60.0, 155.0) / bandEnergy(x, 48000, 8192, 60.0, 4000.0);
    };
    // Without the anti-boom shelf the share grows by about a third (0.35 -> 0.46). It must also not go thin.
    EXPECT_LE(bassShare(out), bassShare(in) * 1.1) << bassShare(in) << " -> " << bassShare(out);
    EXPECT_GE(bassShare(out), bassShare(in) * 0.6) << bassShare(in) << " -> " << bassShare(out);
}
