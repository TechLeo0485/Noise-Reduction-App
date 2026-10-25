#include "audio/VoiceProcessingChain.h"
#include "audio/isolation/VoiceIsolator.h"
#include "settings/Presets.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace vs;

namespace {

constexpr std::size_t kFrame = VoiceIsolator::kFrameSize;

// Identity stand-in for chunk.onnx (see test_voxlock_stream.cpp): the isolated output equals the input, so
// every delivered frame can be checked against the delayed microphone signal.
class IdentityModel final : public voxlock::IChunkModel {
public:
    IdentityModel(int k, std::atomic<int>* failAfter, int sleepMs) : k_(k), failAfter_(failAfter), sleepMs_(sleepMs) {
        for (int i = 0; i < voxlock::kNfft; ++i) {
            const auto w = static_cast<float>(0.5 - 0.5 * std::cos(test::kTwoPi * i / voxlock::kNfft));
            window2_.push_back(w * w);
        }
    }
    [[nodiscard]] int framesPerRun() const noexcept override { return k_; }
    bool run(const float* wav, const float*, const float*, float* frames, float*, float*) noexcept override {
        if (sleepMs_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs_));
        }
        if (failAfter_ != nullptr && failAfter_->fetch_sub(1) <= 0) {
            return false;
        }
        for (int j = 0; j < k_; ++j) {
            for (int i = 0; i < voxlock::kNfft; ++i) {
                frames[j * voxlock::kNfft + i] =
                    window2_[static_cast<std::size_t>(i)] * wav[j * voxlock::kHop + i] / voxlock::kOutputGain;
            }
        }
        return true;
    }

private:
    int k_;
    std::atomic<int>* failAfter_;
    int sleepMs_;
    std::vector<float> window2_;
};

VoiceIsolator::ModelFactory identity(std::atomic<int>* failAfter = nullptr, int sleepMs = 0) {
    return [failAfter, sleepMs](int k) { return std::make_unique<IdentityModel>(k, failAfter, sleepMs); };
}

// Speech-band test signal (well inside the 16 kHz model band).
std::vector<float> testSignal(std::size_t count) {
    std::vector<float> x(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        x[i] = static_cast<float>(0.2 * std::sin(test::kTwoPi * 300.0 * t) + 0.1 * std::sin(test::kTwoPi * 1210.0 * t) +
                                  0.05 * std::sin(test::kTwoPi * 2730.0 * t));
    }
    return x;
}

struct RunResult {
    std::vector<float> out;
    std::vector<IsolationFrameInfo> info;
};

// Feeds frames at roughly real-time pace so the worker thread keeps up.
RunResult run(VoiceIsolator& isolator, const std::vector<float>& x, bool enabled, int pauseMs = 2) {
    RunResult r;
    r.out = x;
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame) {
        IsolationFrameInfo info;
        isolator.process(r.out.data() + off, enabled, info);
        r.info.push_back(info);
        if (pauseMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
        }
    }
    return r;
}

// Every frame must be either silence or the input delayed by the reported latency - never the
// undelayed microphone.
void expectFailClosed(const std::vector<float>& x, const RunResult& r, std::size_t firstFrame = 0) {
    for (std::size_t f = firstFrame; f < r.info.size(); ++f) {
        const float* y = r.out.data() + f * kFrame;
        const bool fadeIn = f == 0 || !r.info[f - 1].delivering; // first frame after silence is faded in
        if (!r.info[f].delivering) {
            for (std::size_t i = 0; i < kFrame; ++i) {
                ASSERT_EQ(y[i], 0.0f) << "frame " << f << " not silent while isolation could not deliver";
            }
            continue;
        }
        const auto latency = static_cast<std::size_t>(r.info[f].latencySamples);
        ASSERT_GE(latency, kFrame) << "isolated audio cannot be ahead of the microphone";
        for (std::size_t i = 0; i < kFrame; ++i) {
            const std::size_t src = f * kFrame + i - latency;
            const float gain = fadeIn ? static_cast<float>(i + 1) / static_cast<float>(kFrame) : 1.0f;
            ASSERT_NEAR(y[i], gain * x[src], 0.02f) << "frame " << f << " sample " << i;
        }
    }
}

} // namespace

TEST(VoiceIsolator, DisabledPassesMicrophoneUntouched) {
    VoiceIsolator isolator;
    const std::vector<float> x = testSignal(48000);
    const RunResult r = run(isolator, x, false, 0);
    EXPECT_EQ(r.out, x);
}

TEST(VoiceIsolator, EnabledWithoutModelIsSilent) {
    VoiceIsolator isolator; // never initialised: no runtime, no model, no voice
    const std::vector<float> x = testSignal(48000);
    const RunResult r = run(isolator, x, true, 0);
    for (float v : r.out) {
        ASSERT_EQ(v, 0.0f);
    }
}

TEST(VoiceIsolator, DeliversDelayedIsolatedVoiceUnderLatencyTarget) {
    VoiceIsolator isolator;
    isolator.initializeWithModel(identity(), 2);
    const std::vector<float> x = testSignal(48000 * 2);
    const RunResult r = run(isolator, x, true);
    expectFailClosed(x, r);
    std::size_t delivered = 0;
    for (const auto& i : r.info) {
        if (i.delivering) {
            ++delivered;
            EXPECT_LT(i.latencySamples, 4800) << "added latency must stay below 100 ms";
        }
    }
    EXPECT_GT(delivered, r.info.size() / 2);
    EXPECT_GT(isolator.metrics().isolatedFrames.load(), 0u);
}

TEST(VoiceIsolator, InferenceFailureMutesInsteadOfPassingMicrophone) {
    std::atomic<int> failAfter{30};
    VoiceIsolator isolator;
    isolator.initializeWithModel(identity(&failAfter), 2);
    const std::vector<float> x = testSignal(48000 * 2);
    const RunResult r = run(isolator, x, true);
    expectFailClosed(x, r);
    EXPECT_GT(isolator.metrics().modelFailures.load(), 0u);
    // After the model breaks for good, the output stays silent.
    for (std::size_t i = r.out.size() - 48000 / 2; i < r.out.size(); ++i) {
        ASSERT_EQ(r.out[i], 0.0f);
    }
}

TEST(VoiceIsolator, SlowWorkerInsertsSilenceNotMicrophone) {
    VoiceIsolator isolator;
    isolator.initializeWithModel(identity(nullptr, 25), 2); // 25 ms per 16 ms chunk: cannot keep up
    const std::vector<float> x = testSignal(48000);
    const RunResult r = run(isolator, x, true, 5);
    expectFailClosed(x, r);
}

TEST(VoiceIsolator, DeletingTheVoiceSilencesOutput) {
    VoiceIsolator isolator;
    isolator.initializeWithModel(identity(), 2);
    const std::vector<float> x = testSignal(48000);
    run(isolator, x, true);
    isolator.deleteEnrollment();
    isolator.waitIdle();
    EXPECT_EQ(isolator.state(), IsolationState::NeedsEnrollment);
    const RunResult r = run(isolator, x, true);
    for (std::size_t i = 2 * kFrame; i < r.out.size(); ++i) { // the switch takes effect within a frame or two
        ASSERT_EQ(r.out[i], 0.0f);
    }
}

TEST(VoiceIsolator, SwitchingOnFadesMicrophoneOutWithinTwoMilliseconds) {
    VoiceIsolator isolator;
    isolator.initializeWithModel(identity(), 2);
    const std::vector<float> x = testSignal(48000);
    run(isolator, x, false, 0);
    std::vector<float> frame(x.begin(), x.begin() + kFrame);
    IsolationFrameInfo info;
    isolator.process(frame.data(), true, info);
    EXPECT_FALSE(info.delivering);
    for (std::size_t i = 96; i < kFrame; ++i) {
        ASSERT_EQ(frame[i], 0.0f);
    }
}

TEST(VoiceIsolator, ChainRunsIsolationBeforeSuppression) {
    VoiceProcessingChain chain;
    chain.isolator().initializeWithModel(identity(), 2);
    EngineParameters p;
    p.set(ParamId::IsolationEnabled, 1.0f);
    std::vector<float> x = testSignal(48000);
    FrameDiagnostics d;
    bool delivered = false;
    for (std::size_t off = 0; off + kFrame <= x.size(); off += kFrame) {
        chain.process(x.data() + off, p, d);
        delivered = delivered || d.isolationDelivering;
        EXPECT_TRUE(d.isolationEnabled);
        if (d.isolationDelivering) {
            EXPECT_GE(d.algorithmicLatencySamples, d.isolationLatencySamples);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(delivered);
}

TEST(VoiceIsolator, RecordingCapturesFramesAndResamplesTo16k) {
    VoiceIsolator isolator;
    ASSERT_TRUE(isolator.startRecording(3.0));
    const std::vector<float> x = test::sine(500.0, 48000.0, 48000 * 3 + kFrame, 0.3f);
    run(isolator, x, false, 0);
    ASSERT_EQ(isolator.recordingState(), VoiceIsolator::RecordingState::Ready);
    const std::vector<float> y = isolator.takeRecording16k();
    EXPECT_NEAR(static_cast<double>(y.size()), 16000.0 * 3, 4.0);
    EXPECT_NEAR(test::rmsOf(y, 1000), 0.3 / std::sqrt(2.0), 0.01);
    EXPECT_EQ(isolator.recordingState(), VoiceIsolator::RecordingState::Idle);
}

TEST(Parameters, IsolationIsOffByDefaultAndNotPresetScoped) {
    EngineParameters p;
    EXPECT_FALSE(p.getBool(ParamId::IsolationEnabled));
    EXPECT_EQ(std::string(paramSpec(ParamId::IsolationEnabled).key), "isolationEnabled");
    // Applying a preset must never switch strict isolation off behind the user's back.
    p.set(ParamId::IsolationEnabled, 1.0f);
    for (const Preset& preset : builtInPresets()) {
        applyPreset(preset, p);
        EXPECT_TRUE(p.getBool(ParamId::IsolationEnabled)) << preset.name;
    }
}
