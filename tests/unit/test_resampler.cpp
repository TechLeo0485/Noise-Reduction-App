#include "audio/buffers/Resampler.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace vs;

namespace {
std::vector<float> resampleAll(Resampler& rs, const std::vector<float>& input, std::size_t block) {
    std::vector<float> out;
    std::vector<float> buffer(block * 4 + 64);
    for (std::size_t off = 0; off < input.size(); off += block) {
        const std::size_t n = std::min(block, input.size() - off);
        std::size_t written = 0;
        while (written < n) {
            written += rs.write(input.data() + off + written, n - written);
            const std::size_t got = rs.read(buffer.data(), buffer.size());
            out.insert(out.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
        }
    }
    return out;
}
} // namespace

TEST(Resampler, Upsample44100To48000PreservesFrequencyAndLevel) {
    Resampler rs;
    rs.configure(44100.0, 48000.0, 512);
    const std::vector<float> input = test::sine(1000.0, 44100.0, 44100, 0.5f);
    const std::vector<float> out = resampleAll(rs, input, 441);

    EXPECT_NEAR(static_cast<double>(out.size()), 48000.0, 64.0);
    EXPECT_NEAR(test::zeroCrossingFrequency(out, 48000.0, 1000), 1000.0, 1.0);
    EXPECT_NEAR(test::rmsOf(out, 4800, out.size() - 4800), 0.5 / std::sqrt(2.0), 0.01);
}

TEST(Resampler, Downsample96000To48000RejectsAliases) {
    Resampler rs;
    rs.configure(96000.0, 48000.0, 1024);
    const std::vector<float> passband = resampleAll(rs, test::sine(1000.0, 96000.0, 96000, 0.5f), 960);
    rs.reset();
    const std::vector<float> alias = resampleAll(rs, test::sine(40000.0, 96000.0, 96000, 0.5f), 960);

    const double pass = test::rmsOf(passband, 4800, passband.size() - 4800);
    const double stop = test::rmsOf(alias, 4800, alias.size() - 4800);
    EXPECT_NEAR(pass, 0.5 / std::sqrt(2.0), 0.01);
    EXPECT_LT(test::toDb(stop / pass), -30.0); // 40 kHz must not fold back to 8 kHz
}

TEST(Resampler, InputRequiredForIsSufficient) {
    Resampler rs;
    rs.configure(48000.0, 44100.0, 2048);
    const std::vector<float> input = test::sine(440.0, 48000.0, 48000, 0.3f);
    std::size_t consumed = 0;
    std::vector<float> out(441);
    for (int block = 0; block < 50; ++block) {
        const std::size_t need = rs.inputRequiredFor(out.size());
        ASSERT_LE(consumed + need, input.size());
        ASSERT_EQ(rs.write(input.data() + consumed, need), need);
        consumed += need;
        ASSERT_EQ(rs.read(out.data(), out.size()), out.size()) << "block " << block;
    }
}

TEST(Resampler, StepScaleChangesConsumptionRate) {
    Resampler normal;
    Resampler fast;
    normal.configure(48000.0, 48000.0, 1024);
    fast.configure(48000.0, 48000.0, 1024);
    fast.setStepScale(1.005);
    std::size_t normalIn = 0, fastIn = 0;
    std::vector<float> zeros(2048, 0.0f), out(480);
    for (int i = 0; i < 100; ++i) {
        std::size_t n = normal.inputRequiredFor(480);
        normal.write(zeros.data(), n);
        normalIn += n;
        normal.read(out.data(), 480);
        n = fast.inputRequiredFor(480);
        fast.write(zeros.data(), n);
        fastIn += n;
        fast.read(out.data(), 480);
    }
    // 48000 outputs: nominal consumes ~48000 inputs, +0.5 % consumes ~240 more.
    EXPECT_NEAR(static_cast<double>(fastIn) - static_cast<double>(normalIn), 240.0, 10.0);
}
