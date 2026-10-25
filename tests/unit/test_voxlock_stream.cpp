#include "ai/voxlock/OnnxModelPatch.h"
#include "ai/voxlock/VoxLockEnrollment.h"
#include "ai/voxlock/VoxLockStream.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace vs;
using namespace vs::voxlock;

namespace {

// Stands in for chunk.onnx: returns each analysis frame times the squared Hann window and the inverse of
// the output gain, so the reference overlap-add must reconstruct the input exactly. Also checks that the
// LSTM state is carried from call to call (h[0] counts the calls).
class IdentityModel final : public IChunkModel {
public:
    explicit IdentityModel(int k) : k_(k) {
        for (int i = 0; i < kNfft; ++i) {
            const auto w = static_cast<float>(0.5 - 0.5 * std::cos(test::kTwoPi * i / kNfft));
            window2_.push_back(w * w);
        }
    }
    [[nodiscard]] int framesPerRun() const noexcept override { return k_; }
    bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut, float* cOut) noexcept override {
        stateSeen.push_back(hIn[0]);
        for (int j = 0; j < k_; ++j) {
            for (int i = 0; i < kNfft; ++i) {
                frames[j * kNfft + i] = window2_[static_cast<std::size_t>(i)] * wav[j * kHop + i] / kOutputGain;
            }
        }
        for (std::size_t i = 0; i < kStateSize; ++i) {
            hOut[i] = hIn[i] + 1.0f;
            cOut[i] = cIn[i];
        }
        return !failNow;
    }
    std::vector<float> stateSeen;
    bool failNow = false;

private:
    int k_;
    std::vector<float> window2_;
};

std::vector<float> runStream(int k, const std::vector<float>& x, std::size_t block) {
    IdentityModel model(k);
    Stream stream(k, static_cast<int>(block));
    std::vector<float> out;
    std::vector<float> buffer(static_cast<std::size_t>(stream.maxOutputSamples()));
    for (std::size_t i = 0; i < x.size(); i += block) {
        const int n = stream.push(model, x.data() + i, static_cast<int>(std::min(block, x.size() - i)), buffer.data());
        EXPECT_GE(n, 0);
        out.insert(out.end(), buffer.begin(), buffer.begin() + n);
    }
    return out;
}

} // namespace

TEST(VoxLockStream, ReconstructsInputSampleAligned) {
    const std::vector<float> x = test::whiteNoise(16000, 0.1f);
    for (int k : {1, 2, 3, 5}) {
        const std::vector<float> y = runStream(k, x, 160);
        ASSERT_GT(y.size(), 15000u);
        double maxError = 0.0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            maxError = std::max(maxError, static_cast<double>(std::fabs(y[i] - x[i])));
        }
        EXPECT_LT(maxError, 1e-5) << "K=" << k;
    }
}

TEST(VoxLockStream, OutputIsIndependentOfBlockSize) {
    const std::vector<float> x = test::whiteNoise(12000, 0.1f, 7);
    const std::vector<float> a = runStream(2, x, 160);
    for (std::size_t block : {1u, 37u, 128u, 1000u}) {
        const std::vector<float> b = runStream(2, x, block);
        const std::size_t n = std::min(a.size(), b.size());
        ASSERT_GT(n, 10000u);
        for (std::size_t i = 0; i < n; ++i) {
            ASSERT_EQ(a[i], b[i]) << "block " << block << " sample " << i;
        }
    }
}

TEST(VoxLockStream, LatencyIsAlgorithmicPlusChunking) {
    for (int k : {1, 2, 3}) {
        IdentityModel model(k);
        Stream stream(k, 160);
        std::vector<float> buffer(static_cast<std::size_t>(stream.maxOutputSamples()));
        const std::vector<float> block(160, 0.1f);
        for (int i = 0; i < 100; ++i) {
            ASSERT_GE(stream.push(model, block.data(), 160, buffer.data()), 0);
            if (stream.samplesOut() > 0) {
                const long long lag = stream.samplesIn() - stream.samplesOut();
                EXPECT_GE(lag, kAlgorithmicLatencySamples) << "K=" << k;          // 24 ms
                EXPECT_LT(lag, kAlgorithmicLatencySamples + k * kHop + 160) << "K=" << k; // + chunk + block
            }
        }
    }
}

TEST(VoxLockStream, CarriesLstmStateAcrossCalls) {
    IdentityModel model(2);
    Stream stream(2, 160);
    std::vector<float> buffer(static_cast<std::size_t>(stream.maxOutputSamples()));
    const std::vector<float> x = test::whiteNoise(8000, 0.1f);
    for (std::size_t i = 0; i + 160 <= x.size(); i += 160) {
        stream.push(model, x.data() + i, 160, buffer.data());
    }
    ASSERT_GT(model.stateSeen.size(), 10u);
    for (std::size_t i = 0; i < model.stateSeen.size(); ++i) {
        EXPECT_EQ(model.stateSeen[i], static_cast<float>(i));
    }
    stream.reset();
    model.stateSeen.clear();
    for (std::size_t i = 0; i + 160 <= 2000; i += 160) {
        stream.push(model, x.data() + i, 160, buffer.data());
    }
    ASSERT_FALSE(model.stateSeen.empty());
    EXPECT_EQ(model.stateSeen.front(), 0.0f); // reset starts from a zero state
}

TEST(VoxLockStream, ReportsModelFailure) {
    IdentityModel model(2);
    model.failNow = true;
    Stream stream(2, 1000);
    std::vector<float> buffer(static_cast<std::size_t>(stream.maxOutputSamples()));
    const std::vector<float> x(1000, 0.1f);
    EXPECT_EQ(stream.push(model, x.data(), 1000, buffer.data()), -1);
}

TEST(VoxLockEnrollment, FitSamplePicksLoudestThreeSeconds) {
    std::vector<float> audio(16000 * 10, 0.0f);
    const std::vector<float> loud = test::sine(220.0, 16000.0, 16000 * 3, 0.5f);
    std::copy(loud.begin(), loud.end(), audio.begin() + 16000 * 4); // loud part at 4..7 s
    std::vector<float> sample;
    std::string error;
    ASSERT_TRUE(fitSample(audio, sample, error));
    ASSERT_EQ(sample.size(), static_cast<std::size_t>(kEnrollSamples));
    EXPECT_GT(test::rmsOf(sample), 0.3);

    std::vector<float> tooShort(16000 * 2, 0.1f);
    EXPECT_FALSE(fitSample(tooShort, sample, error));
}

TEST(VoxLockEnrollment, ValidationRejectsSilenceAndSteadyNoise) {
    std::string error;
    EXPECT_FALSE(validateVoiceSample(std::vector<float>(16000 * 5, 0.0f), error));
    EXPECT_FALSE(validateVoiceSample(test::whiteNoise(16000 * 5, 0.1f), error)) << "steady noise is not speech";

    // Speech-like: noise bursts (syllables) with pauses.
    std::vector<float> speech = test::whiteNoise(16000 * 5, 0.1f);
    for (std::size_t i = 0; i < speech.size(); ++i) {
        if ((i / 2400) % 3 == 2) {
            speech[i] *= 0.01f;
        }
    }
    EXPECT_TRUE(validateVoiceSample(speech, error)) << error;
}

TEST(VoxLockEnrollment, SaveLoadRoundTrip) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / L"vs_test_voice_sample.wav";
    const std::vector<float> x = test::whiteNoise(16000 * 4, 0.1f);
    std::string error;
    ASSERT_TRUE(saveVoiceSample(path, x, error)) << error;
    std::vector<float> y;
    ASSERT_TRUE(loadVoiceSample(path, y, error)) << error;
    EXPECT_EQ(x, y);
    std::filesystem::remove(path);
}

namespace {

void varint(std::vector<char>& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

void bytesField(std::vector<char>& out, std::uint32_t number, const std::vector<char>& payload) {
    varint(out, (static_cast<std::uint64_t>(number) << 3) | 2);
    varint(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<char> str(const std::string& s) {
    return {s.begin(), s.end()};
}

std::vector<char> valueInfo(const std::string& name) {
    std::vector<char> v;
    bytesField(v, 1, str(name));
    return v;
}

bool contains(const std::vector<char>& haystack, const std::vector<char>& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

} // namespace

TEST(OnnxModelPatch, MovesInputsToInitializers) {
    // ModelProto { ir_version: 8, graph: { node(raw), input: wav, input: context, output: y } }
    std::vector<char> graph;
    bytesField(graph, 1, str("node-bytes"));
    bytesField(graph, 11, valueInfo("wav"));
    bytesField(graph, 11, valueInfo("context"));
    bytesField(graph, 12, valueInfo("y"));
    std::vector<char> model;
    varint(model, (1u << 3) | 0);
    varint(model, 8);
    bytesField(model, 7, graph);

    const std::vector<float> data{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    std::vector<char> out;
    std::string error;
    ASSERT_TRUE(bindInputsAsConstants(model, {{"context", {1, 2, 3}, data.data()}}, out, error)) << error;

    std::vector<char> expectedGraph;
    bytesField(expectedGraph, 1, str("node-bytes"));
    bytesField(expectedGraph, 11, valueInfo("wav"));
    bytesField(expectedGraph, 12, valueInfo("y"));
    std::vector<char> tensor;
    for (int d : {1, 2, 3}) {
        varint(tensor, (1u << 3) | 0);
        varint(tensor, static_cast<std::uint64_t>(d));
    }
    varint(tensor, (2u << 3) | 0);
    varint(tensor, 1); // FLOAT
    bytesField(tensor, 8, str("context"));
    const auto* raw = reinterpret_cast<const char*>(data.data());
    bytesField(tensor, 9, std::vector<char>(raw, raw + data.size() * sizeof(float)));
    bytesField(expectedGraph, 5, tensor);
    std::vector<char> expected;
    varint(expected, (1u << 3) | 0);
    varint(expected, 8);
    bytesField(expected, 7, expectedGraph);
    EXPECT_EQ(out, expected);
    EXPECT_FALSE(contains(out, valueInfo("context")));

    EXPECT_FALSE(bindInputsAsConstants(model, {{"missing", {1}, data.data()}}, out, error));
    EXPECT_FALSE(bindInputsAsConstants(std::vector<char>{'\xff'}, {{"context", {1}, data.data()}}, out, error));
}
