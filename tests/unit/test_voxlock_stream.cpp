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

TEST(VoxLockEnrollment, FitSamplePicksTheSpeech) {
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

TEST(VoxLockEnrollment, LongRecordingGivesTypicalSpeechNotTheShout) {
    // 60 s of speech-like bursts at a normal level, one clipped shout at 40 s and a whisper at 10 s.
    constexpr std::size_t kRate = 16000;
    std::vector<float> audio = test::whiteNoise(kRate * 60, 0.08f);
    for (std::size_t i = 0; i < audio.size(); ++i) {
        if ((i / 2400) % 4 == 3) {
            audio[i] *= 0.01f; // pauses between syllables
        }
        const double t = static_cast<double>(i) / kRate;
        if (t >= 40.0 && t < 44.0) {
            audio[i] = std::clamp(audio[i] * 12.0f, -1.0f, 1.0f); // shout, clipping
        } else if (t >= 10.0 && t < 14.0) {
            audio[i] *= 0.1f; // whisper
        }
    }
    std::vector<float> sample;
    std::string error;
    ASSERT_TRUE(fitSample(audio, sample, error));
    ASSERT_EQ(sample.size(), static_cast<std::size_t>(kEnrollSamples));
    const double level = test::rmsOf(sample);
    EXPECT_GT(level, 0.04);  // not the whisper
    EXPECT_LT(level, 0.2);   // not the shout
    EXPECT_TRUE(validateVoiceSample(audio, error)) << error;
}

TEST(VoxLockEnrollment, RecordingSavesAsPcm16) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / L"vs_test_voice_recording.wav";
    const std::vector<float> x = test::whiteNoise(16000 * 2, 0.1f);
    std::string error;
    ASSERT_TRUE(saveRecording(path, x, error)) << error;
    EXPECT_EQ(std::filesystem::file_size(path), 44u + x.size() * 2);
    std::filesystem::remove(path);
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

namespace {

// Speech-like noise bursts with pauses, `seconds` long, at `level` (RMS of the bursts).
std::vector<float> speechLike(double seconds, float level, unsigned seed) {
    std::vector<float> x = test::whiteNoise(static_cast<std::size_t>(seconds * kSampleRate), level, seed);
    for (std::size_t i = 0; i < x.size(); ++i) {
        if ((i / 2400) % 4 == 3) {
            x[i] *= 0.01f;
        }
    }
    return x;
}

// A guided recording: 20 s each of normal, louder, quieter, higher (here: other noise), lower and free talk.
std::vector<float> guidedRecording() {
    std::vector<float> audio;
    const float levels[] = {0.08f, 0.2f, 0.03f, 0.08f, 0.1f, 0.08f};
    for (unsigned k = 0; k < 6; ++k) {
        const std::vector<float> part = speechLike(20.0, levels[k], 100 + k);
        audio.insert(audio.end(), part.begin(), part.end());
    }
    return audio;
}

bool overlapping(const SampleRange& a, const SampleRange& b) {
    return a.begin < b.end && b.begin < a.end;
}

} // namespace

TEST(VoxLockEnrollment, PlanCoversEverySectionOfALongRecording) {
    const std::vector<float> audio = guidedRecording();
    const EnrollmentPlan plan = planEnrollment(audio);
    ASSERT_GE(plan.candidates.size(), 4u);
    EXPECT_EQ(plan.candidates.front().label, "most typical");
    EXPECT_EQ(plan.candidates.back().label, "mix");
    EXPECT_EQ(plan.tests.size(), 6u); // one held-out test segment per section
    for (const SampleCandidate& c : plan.candidates) {
        EXPECT_EQ(c.sample.size(), static_cast<std::size_t>(kEnrollSamples)) << c.label;
        ASSERT_FALSE(c.sources.empty());
    }
    // A section's own candidate never overlaps that section's test segment.
    for (std::size_t c = 1; c + 1 < plan.candidates.size(); ++c) {
        const SampleRange& source = plan.candidates[c].sources.front();
        const std::size_t section = source.begin / (audio.size() / 6);
        EXPECT_FALSE(overlapping(source, plan.tests[section])) << plan.candidates[c].label;
    }
    // The candidates come from different styles: the quiet section's window is clearly quieter.
    double quietest = 1.0;
    double loudest = 0.0;
    for (const SampleCandidate& c : plan.candidates) {
        quietest = std::min(quietest, test::rmsOf(c.sample));
        loudest = std::max(loudest, test::rmsOf(c.sample));
    }
    EXPECT_GT(loudest / quietest, 3.0);
}

TEST(VoxLockEnrollment, ShortRecordingHasNothingToChoose) {
    const EnrollmentPlan plan = planEnrollment(speechLike(8.0, 0.1f, 7));
    ASSERT_EQ(plan.candidates.size(), 1u);
    EXPECT_TRUE(plan.tests.empty());
    EXPECT_EQ(planEnrollment(speechLike(2.0, 0.1f, 7)).candidates.size(), 0u);

    std::size_t calls = 0;
    const auto isolate = [&](const std::vector<float>&, const std::vector<std::vector<float>>&,
                             std::vector<std::vector<float>>&) {
        ++calls;
        return true;
    };
    EXPECT_EQ(chooseCandidate(speechLike(8.0, 0.1f, 7), plan, isolate), 0u);
    EXPECT_EQ(calls, 0u);
}

TEST(VoxLockEnrollment, KeptLevelMeasuresSpeechOnly) {
    const std::vector<float> in = speechLike(2.0, 0.1f, 3);
    EXPECT_NEAR(keptDb(in.data(), in.data(), in.size()), 0.0, 0.01);
    std::vector<float> half = in;
    for (float& v : half) {
        v *= 0.5f;
    }
    EXPECT_NEAR(keptDb(in.data(), half.data(), in.size()), -6.02, 0.05);
    const std::vector<float> silence(in.size(), 0.0f);
    EXPECT_EQ(keptDb(in.data(), silence.data(), in.size()), -40.0);
}

TEST(VoxLockEnrollment, ChoosesTheCandidateThatKeepsEveryStyle) {
    const std::vector<float> audio = guidedRecording();
    const EnrollmentPlan plan = planEnrollment(audio);
    ASSERT_GE(plan.candidates.size(), 3u);

    // Stand-in for the network: the most typical window "locks on wrong" and cuts quiet speech by 30 dB (as a bad
    // 3 s window really can, see the voice-sample evaluation); candidate 2 keeps everything; the rest lose 3 dB.
    const std::vector<float>& bad = plan.candidates[0].sample;
    const std::vector<float>& good = plan.candidates[2].sample;
    const auto isolate = [&](const std::vector<float>& sample, const std::vector<std::vector<float>>& inputs,
                             std::vector<std::vector<float>>& outputs) {
        outputs.clear();
        for (const std::vector<float>& input : inputs) {
            float gain = 0.7f;
            if (sample == good) {
                gain = 1.0f;
            } else if (sample == bad && test::rmsOf(input) < 0.04) {
                gain = 0.03f;
            }
            std::vector<float> out = input;
            for (float& v : out) {
                v *= gain;
            }
            outputs.push_back(std::move(out));
        }
        return true;
    };
    std::vector<std::size_t> progress;
    std::vector<double> scores;
    const std::size_t best =
        chooseCandidate(audio, plan, isolate, [&](std::size_t i, std::size_t) { progress.push_back(i); }, &scores);
    EXPECT_EQ(best, 2u);
    EXPECT_EQ(progress.size(), plan.candidates.size());
    EXPECT_LT(scores[0], scores[2] - 20.0);
}

TEST(VoxLockEnrollment, KeepsTheTypicalWindowUnlessAnotherIsClearlyBetter) {
    const std::vector<float> audio = guidedRecording();
    const EnrollmentPlan plan = planEnrollment(audio);
    // All candidates equal, then the others 0.25 dB better everywhere (0.5 in the score, mean + worst): the most
    // typical window stays. 1.5 dB better everywhere: another one wins.
    for (float otherGain : {1.0f, 1.03f, 1.19f}) {
        const auto isolate = [&](const std::vector<float>& sample, const std::vector<std::vector<float>>& inputs,
                                 std::vector<std::vector<float>>& outputs) {
            outputs.clear();
            const float gain = sample == plan.candidates[0].sample ? 0.9f : 0.9f * otherGain;
            for (const std::vector<float>& input : inputs) {
                std::vector<float> out = input;
                for (float& v : out) {
                    v *= gain;
                }
                outputs.push_back(std::move(out));
            }
            return true;
        };
        EXPECT_EQ(chooseCandidate(audio, plan, isolate) == 0u, otherGain < 1.1f) << otherGain;
    }
    // Failing candidates are skipped.
    const auto failing = [&](const std::vector<float>& sample, const std::vector<std::vector<float>>& inputs,
                             std::vector<std::vector<float>>& outputs) {
        if (sample == plan.candidates[0].sample) {
            return false;
        }
        outputs = inputs;
        return true;
    };
    EXPECT_NE(chooseCandidate(audio, plan, failing), 0u);
}

TEST(VoxLockEnrollment, LoadsThePcm16Recording) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / L"vs_test_voice_recording16.wav";
    const std::vector<float> x = test::whiteNoise(16000 * 2, 0.1f);
    std::string error;
    ASSERT_TRUE(saveRecording(path, x, error)) << error;
    std::vector<float> y;
    ASSERT_TRUE(loadVoiceSample(path, y, error)) << error;
    ASSERT_EQ(y.size(), x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        ASSERT_NEAR(y[i], x[i], 1.0 / 32768.0 + 1e-6);
    }
    std::filesystem::remove(path);
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

TEST(OnnxModelPatch, SetsAnAttributeOfEveryMatchingNode) {
    // AttributeProto { name, i, type: INT }
    const auto intAttribute = [](const std::string& name, std::uint64_t value) {
        std::vector<char> a;
        bytesField(a, 1, str(name));
        varint(a, (3u << 3) | 0);
        varint(a, value);
        varint(a, (20u << 3) | 0);
        varint(a, 2);
        return a;
    };
    // NodeProto { input, op_type, attribute... }
    const auto node = [&](const std::string& op, const std::vector<std::vector<char>>& attributes) {
        std::vector<char> n;
        bytesField(n, 1, str("x"));
        bytesField(n, 4, str(op));
        for (const auto& a : attributes) {
            bytesField(n, 5, a);
        }
        return n;
    };
    const auto modelWith = [&](std::uint64_t poolValue) {
        std::vector<char> graph;
        bytesField(graph, 1, node("AveragePool", {intAttribute("ceil_mode", 1), intAttribute("count_include_pad", poolValue)}));
        bytesField(graph, 1, node("Relu", {intAttribute("count_include_pad", 1)}));
        bytesField(graph, 1, node("AveragePool", {intAttribute("ceil_mode", 1)}));
        bytesField(graph, 11, valueInfo("x"));
        std::vector<char> model;
        varint(model, (1u << 3) | 0);
        varint(model, 8);
        bytesField(model, 7, graph);
        return model;
    };

    std::vector<char> out;
    std::string error;
    int changed = -1;
    ASSERT_TRUE(setNodeAttribute(modelWith(1), "AveragePool", "count_include_pad", 0, out, changed, error)) << error;
    EXPECT_EQ(changed, 1); // the Relu has a same-named attribute and the second pool has none: both untouched
    // The attribute keeps its name and type; only the value moves after them.
    std::vector<char> expected;
    {
        std::vector<char> patchedAttribute;
        bytesField(patchedAttribute, 1, str("count_include_pad"));
        varint(patchedAttribute, (20u << 3) | 0);
        varint(patchedAttribute, 2);
        varint(patchedAttribute, (3u << 3) | 0);
        varint(patchedAttribute, 0);
        std::vector<char> graph;
        bytesField(graph, 1, node("AveragePool", {intAttribute("ceil_mode", 1), patchedAttribute}));
        bytesField(graph, 1, node("Relu", {intAttribute("count_include_pad", 1)}));
        bytesField(graph, 1, node("AveragePool", {intAttribute("ceil_mode", 1)}));
        bytesField(graph, 11, valueInfo("x"));
        varint(expected, (1u << 3) | 0);
        varint(expected, 8);
        bytesField(expected, 7, graph);
    }
    EXPECT_EQ(out, expected);
    EXPECT_FALSE(setNodeAttribute(std::vector<char>{'\xff'}, "AveragePool", "count_include_pad", 0, out, changed, error));
}
