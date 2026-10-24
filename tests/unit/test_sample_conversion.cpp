#include "audio/buffers/SampleConversion.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace vs;

namespace {
StreamFormat makeFormat(SampleType type, std::uint16_t channels, std::uint16_t bits) {
    StreamFormat f;
    f.sampleRate = 48000;
    f.channels = channels;
    f.bitsPerSample = bits;
    f.validBits = bits;
    f.blockAlign = static_cast<std::uint16_t>(channels * bits / 8);
    f.sampleType = type;
    return f;
}
} // namespace

TEST(SampleConversion, Int16Codec) {
    EXPECT_FLOAT_EQ(int16ToFloat(0), 0.0f);
    EXPECT_FLOAT_EQ(int16ToFloat(-32768), -1.0f);
    EXPECT_EQ(floatToInt16(1.0f), 32767);
    EXPECT_EQ(floatToInt16(-1.0f), -32767);
    EXPECT_EQ(floatToInt16(2.5f), 32767);   // clamps
    EXPECT_EQ(floatToInt16(-7.0f), -32767); // clamps
    EXPECT_NEAR(int16ToFloat(floatToInt16(0.25f)), 0.25f, 1.0f / 32768.0f);
}

TEST(SampleConversion, Int24CodecSignExtends) {
    std::uint8_t bytes[3];
    floatToInt24(-0.5f, bytes);
    EXPECT_NEAR(int24ToFloat(bytes), -0.5f, 1e-6f);
    floatToInt24(0.75f, bytes);
    EXPECT_NEAR(int24ToFloat(bytes), 0.75f, 1e-6f);
    const std::uint8_t minusOne[3] = {0x00, 0x00, 0x80};
    EXPECT_FLOAT_EQ(int24ToFloat(minusOne), -1.0f);
}

TEST(SampleConversion, Int32Codec) {
    EXPECT_NEAR(int32ToFloat(floatToInt32(0.3f)), 0.3f, 1e-6f);
    EXPECT_EQ(floatToInt32(5.0f), 2147483647);
}

TEST(SampleConversion, StereoFloatDownmixAverages) {
    const StreamFormat fmt = makeFormat(SampleType::Float32, 2, 32);
    const float interleaved[] = {1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f};
    float mono[3];
    interleavedToMono(interleaved, 3, fmt, mono);
    EXPECT_FLOAT_EQ(mono[0], 0.5f);
    EXPECT_FLOAT_EQ(mono[1], 0.5f);
    EXPECT_FLOAT_EQ(mono[2], 0.0f);
}

TEST(SampleConversion, Int16StereoDownmix) {
    const StreamFormat fmt = makeFormat(SampleType::Int16, 2, 16);
    const std::int16_t interleaved[] = {16384, 16384, -32768, 0};
    float mono[2];
    interleavedToMono(interleaved, 2, fmt, mono);
    EXPECT_NEAR(mono[0], 0.5f, 1e-4f);
    EXPECT_NEAR(mono[1], -0.5f, 1e-4f);
}

TEST(SampleConversion, MonoToMultichannelDuplicatesAndClamps) {
    const StreamFormat fmt = makeFormat(SampleType::Int16, 6, 16); // 5.1 output
    const float mono[] = {0.5f, 3.0f};
    std::vector<std::int16_t> out(12, 0);
    monoToInterleaved(mono, 2, fmt, out.data());
    for (int c = 0; c < 6; ++c) {
        EXPECT_EQ(out[c], floatToInt16(0.5f));
        EXPECT_EQ(out[6 + c], 32767);
    }
}

TEST(SampleConversion, FloatRoundTripThroughDeviceFormat) {
    const StreamFormat fmt = makeFormat(SampleType::Float32, 2, 32);
    const float mono[] = {0.1f, -0.2f, 0.3f};
    float interleaved[6];
    monoToInterleaved(mono, 3, fmt, interleaved);
    float back[3];
    interleavedToMono(interleaved, 3, fmt, back);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(back[i], mono[i]);
    }
}

TEST(SampleConversion, FormatValidation) {
    EXPECT_TRUE(makeFormat(SampleType::Float32, 2, 32).isSupported());
    StreamFormat bad = makeFormat(SampleType::Unsupported, 2, 32);
    EXPECT_FALSE(bad.isSupported());
    StreamFormat wrongAlign = makeFormat(SampleType::Int16, 2, 16);
    wrongAlign.blockAlign = 3;
    EXPECT_FALSE(wrongAlign.isSupported());
}
