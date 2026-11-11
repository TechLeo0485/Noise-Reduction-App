#include "ai/voxlock/NoiseFloorFilter.h"
#include "ai/voxlock/SpeakerCheck.h"
#include "ai/voxlock/VoxLockStream.h"

#include "TestUtils.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace vs;
using namespace vs::voxlock;

namespace {

std::vector<float> fbankTestSignal() {
    std::vector<float> x(4000);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto n = static_cast<double>(i);
        x[i] = static_cast<float>(0.3 * std::sin(test::kTwoPi * 440.0 * n / 16000.0) +
                                  0.1 * std::sin(test::kTwoPi * 3100.0 * n / 16000.0 + 0.5 * std::sin(test::kTwoPi * 3.0 * n / 16000.0)) +
                                  0.05 * std::sin(n * n * 0.0007));
    }
    return x;
}

// Passes the analysed audio straight through (see test_voxlock_stream.cpp), counting calls in h[0].
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
        return true;
    }
    std::vector<float> stateSeen;

private:
    int k_;
    std::vector<float> window2_;
};

std::vector<float> run(Stream& stream, IChunkModel& model, const std::vector<float>& x) {
    constexpr std::size_t kBlock = 160;
    std::vector<float> out;
    std::vector<float> buffer(static_cast<std::size_t>(stream.maxOutputSamples()));
    for (std::size_t i = 0; i < x.size(); i += kBlock) {
        const int n = stream.push(model, x.data() + i, static_cast<int>(std::min(kBlock, x.size() - i)), buffer.data());
        EXPECT_GE(n, 0);
        out.insert(out.end(), buffer.begin(), buffer.begin() + std::max(n, 0));
    }
    return out;
}

double rms(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    double e = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        e += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(e / static_cast<double>(end - begin));
}

// Level of `frequency` in x[begin, end) (single-bin DFT), as an amplitude.
double toneLevel(const std::vector<float>& x, std::size_t begin, std::size_t end, double frequency) {
    double re = 0.0;
    double im = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        const double phase = test::kTwoPi * frequency * static_cast<double>(i) / kSampleRate;
        re += x[i] * std::cos(phase);
        im += x[i] * std::sin(phase);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(end - begin);
}

class FakeSpeakerModel final : public ISpeakerModel {
public:
    bool embed(const float* audio16k, std::size_t count, std::vector<float>& out) override {
        ++calls;
        // A "voice" is the sign of the mean sample: positive and negative DC offsets are two speakers.
        double mean = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            mean += audio16k[i];
        }
        out = {mean >= 0.0 ? 1.0f : 0.0f, mean >= 0.0 ? 0.0f : 1.0f};
        return true;
    }
    int calls = 0;
};

} // namespace

TEST(SpeakerCheck, FbankMatchesKaldi) {
    // Reference: kaldi-native-fbank (dither 0, 80 bins), then the mean of each bin removed.
    const std::vector<float> x = fbankTestSignal();
    std::vector<float> f;
    ASSERT_EQ(computeFbank(x.data(), x.size(), f), 23);
    const auto at = [&](int frame, int bin) { return f[static_cast<std::size_t>(frame) * kFbankBins + static_cast<std::size_t>(bin)]; };
    EXPECT_NEAR(at(0, 0), 2.43804f, 2e-3f);
    EXPECT_NEAR(at(3, 10), -0.03426f, 2e-3f);
    EXPECT_NEAR(at(7, 40), -2.70568f, 2e-3f);
    EXPECT_NEAR(at(20, 79), -3.95612f, 2e-3f);
    EXPECT_NEAR(at(12, 5), -0.04450f, 2e-3f);
    EXPECT_NEAR(at(22, 25), -1.18799f, 2e-3f);
    EXPECT_NEAR(at(15, 60), -4.08738f, 2e-3f);
    EXPECT_EQ(computeFbank(x.data(), 399, f), 0);
}

TEST(SpeakerCheck, ReferenceNeedsSpeech) {
    FakeSpeakerModel model;
    EXPECT_TRUE(referenceEmbedding(model, std::vector<float>(16000, 0.0f)).empty());
    std::vector<float> voice(16000 * 10);
    for (std::size_t i = 0; i < voice.size(); ++i) {
        voice[i] = 0.1f + 0.05f * static_cast<float>(std::sin(test::kTwoPi * 200.0 * static_cast<double>(i) / 16000.0));
    }
    const std::vector<float> ref = referenceEmbedding(model, voice);
    ASSERT_EQ(ref.size(), 2u);
    EXPECT_NEAR(ref[0], 1.0f, 1e-6f);
    EXPECT_EQ(model.calls, 6); // 10 s of speech: six 1.5 s pieces
    EXPECT_NEAR(similarity(ref, {1.0f, 0.0f}), 1.0f, 1e-6f);
    EXPECT_EQ(similarity(ref, {1.0f}), 0.0f);
}

TEST(SpeakerCheck, MutesAnotherVoiceAndReopensForTheUser) {
    VoiceCheck check;
    std::vector<float> speech(160 * 10); // 0.1 s blocks of -23 dBFS "speech"
    for (std::size_t i = 0; i < speech.size(); ++i) {
        speech[i] = 0.1f * static_cast<float>(std::sin(test::kTwoPi * 300.0 * static_cast<double>(i) / 16000.0));
    }
    const std::vector<float> silence(speech.size(), 0.0f);

    // Silence never asks for a check.
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(check.feed(silence.data(), silence.size()), VoiceCheck::Action::None);
    }
    // Speech after a silence passes and is judged after 0.4 s of it.
    int checksAt = -1;
    for (int i = 0; i < 30 && checksAt < 0; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            checksAt = i;
        }
    }
    ASSERT_EQ(checksAt, 3);
    EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
    EXPECT_FALSE(check.decide(0.3f)); // clearly the user: open
    EXPECT_TRUE(check.open());
    // While open, a check needs 0.8 s of speech.
    checksAt = -1;
    for (int i = 0; i < 30 && checksAt < 0; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            checksAt = i;
        }
    }
    ASSERT_GE(checksAt, 2);
    EXPECT_EQ(check.speech().size(), 16000u * 4 / 5);
    EXPECT_FALSE(check.decide(0.2f)); // ambiguous: stays as it is
    EXPECT_TRUE(check.open());
    EXPECT_TRUE(check.decide(0.05f)); // someone else: muted, and the model state is reset
    EXPECT_FALSE(check.open());
    EXPECT_FALSE(check.resetDue());

    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);

    // Speech goes on without a pause (the user talking over the other voice): 0.8 s of it is judged.
    // Meanwhile the model state is reset every 0.5 s, so it can lock onto the user once they speak.
    int checks = 0;
    int resets = 0;
    for (int i = 0; i < 12; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_EQ(check.speech().size(), 16000u * 4 / 5);
            EXPECT_FALSE(check.decide(0.2f)); // not clearly the user: stays muted
            ++checks;
        }
        resets += check.resetDue() ? 1 : 0;
    }
    EXPECT_GE(checks, 2);
    EXPECT_EQ(resets, 2);
    EXPECT_FALSE(check.open());

    // A pause of 0.3 s ends that turn: the model state is reset, and the next speech passes at once.
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(check.feed(silence.data(), silence.size()), VoiceCheck::Action::None);
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    check.feed(silence.data(), silence.size());
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    EXPECT_TRUE(check.resetDue());
    EXPECT_GT(check.speechLevel(), 1e-6f);
    EXPECT_LT(check.speechLevel(), 0.005f); // the "speech" above is 0.005 mean square
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::None);
        EXPECT_TRUE(check.open());
        EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    }
    // After 0.4 s of it (and only it) a check: someone else again, so muted again.
    ASSERT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::Check);
    EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
    EXPECT_TRUE(check.decide(0.1f));
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);

    // That pause was the other voice's: the next turn needs a longer pause (0.6 s).
    for (int i = 0; i < 5; ++i) {
        check.feed(silence.data(), silence.size());
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    check.feed(silence.data(), silence.size());
    // Next turn: the user. The quick check passes, and from then on the gate is simply open.
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    for (int i = 0; i < 4; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_FALSE(check.decide(0.3f));
        }
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    EXPECT_EQ(check.lastScore(), 0.3f);
    // A short pause while open changes nothing.
    for (int i = 0; i < 9; ++i) {
        check.feed(silence.data(), silence.size());
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    // A silence of 1 s ends the user's turn: the next speech passes at once and is judged on its own after 0.4 s,
    // without a model reset (the model still follows the user).
    check.feed(silence.data(), silence.size());
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    EXPECT_FALSE(check.resetDue());
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::None);
        EXPECT_TRUE(check.open());
    }
    ASSERT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::Check);
    EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
    EXPECT_TRUE(check.decide(0.05f)); // someone else talking after the user's silence: muted
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    check.reset();
    EXPECT_TRUE(check.open());
}

TEST(SpeakerCheck, MutedNeedsTwoClearChecksToReopen) {
    VoiceCheck check;
    std::vector<float> speech(160 * 10);
    for (std::size_t i = 0; i < speech.size(); ++i) {
        speech[i] = 0.1f * static_cast<float>(std::sin(test::kTwoPi * 300.0 * static_cast<double>(i) / 16000.0));
    }
    const auto nextCheck = [&]() {
        for (int i = 0; i < 20; ++i) {
            if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
                return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(nextCheck());
    EXPECT_TRUE(check.decide(0.05f));
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // One window that sounds like the user (talking over, no pause) is not enough ...
    ASSERT_TRUE(nextCheck());
    EXPECT_FALSE(check.decide(0.3f));
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // ... and an unclear one in between starts the count again ...
    ASSERT_TRUE(nextCheck());
    EXPECT_FALSE(check.decide(0.2f));
    ASSERT_TRUE(nextCheck());
    EXPECT_FALSE(check.decide(0.3f));
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // ... two in a row reopen.
    ASSERT_TRUE(nextCheck());
    EXPECT_FALSE(check.decide(0.3f));
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
}

TEST(SpeakerCheck, AnotherVoiceWithPausesDoesNotPassEveryPhrase) {
    VoiceCheck check;
    std::vector<float> speech(160 * 10);
    for (std::size_t i = 0; i < speech.size(); ++i) {
        speech[i] = 0.1f * static_cast<float>(std::sin(test::kTwoPi * 300.0 * static_cast<double>(i) / 16000.0));
    }
    const std::vector<float> silence(speech.size(), 0.0f);
    const auto feedSpeech = [&](int blocks, float score) {
        int checks = 0;
        for (int i = 0; i < blocks; ++i) {
            if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
                check.decide(score);
                ++checks;
            }
        }
        return checks;
    };
    const auto pauseUntilArmed = [&]() {
        int blocks = 0;
        while (check.gate() == VoiceCheck::Gate::Muted && blocks < 100) {
            check.feed(silence.data(), silence.size());
            ++blocks;
        }
        return blocks;
    };
    feedSpeech(15, 0.05f);
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::Muted);

    // The other person keeps talking with pauses: each phrase that passes makes the next pause longer.
    EXPECT_EQ(pauseUntilArmed(), 3);
    feedSpeech(4, 0.05f);
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    EXPECT_EQ(pauseUntilArmed(), 6);
    feedSpeech(4, 0.05f);
    EXPECT_EQ(pauseUntilArmed(), 9);
    feedSpeech(4, 0.05f);
    EXPECT_EQ(pauseUntilArmed(), 12);
    feedSpeech(4, 0.05f);
    // ... up to 1.2 s; ordinary 0.3 s pauses between phrases now stay muted.
    for (int i = 0; i < 3; ++i) {
        check.feed(silence.data(), silence.size());
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // A long silence forgets that.
    for (int i = 0; i < 45; ++i) {
        check.feed(silence.data(), silence.size());
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    feedSpeech(4, 0.05f);
    EXPECT_EQ(pauseUntilArmed(), 6); // one strike again (the trial above)

    // An ambiguous quick check is not enough: judged again on 0.8 s, still passing meanwhile ...
    for (int i = 0; i < 4; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
            EXPECT_FALSE(check.decide(0.18f));
        }
    }
    EXPECT_TRUE(check.open());
    int checks = 0;
    for (int i = 0; i < 4; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_EQ(check.speech().size(), 16000u * 4 / 5);
            EXPECT_TRUE(check.decide(0.18f)); // ... and muted if it is still not clearly the user
            ++checks;
        }
    }
    EXPECT_EQ(checks, 1);
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    pauseUntilArmed();
    // Clearly the user on the quick check: open.
    for (int i = 0; i < 4; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_FALSE(check.decide(0.3f));
        }
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    // Open now, and checked again (on 0.8 s of its speech) every 0.3 s.
    bool muted = false;
    for (int i = 0; i < 8 && !muted; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_EQ(check.speech().size(), 16000u * 4 / 5);
            muted = check.decide(0.05f);
        }
    }
    EXPECT_TRUE(muted);
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
}

TEST(SpeakerCheck, OldSpeechIsNotUsed) {
    VoiceCheck check;
    std::vector<float> speech(160 * 10);
    for (std::size_t i = 0; i < speech.size(); ++i) {
        speech[i] = 0.1f * static_cast<float>(std::sin(test::kTwoPi * 300.0 * static_cast<double>(i) / 16000.0));
    }
    const std::vector<float> silence(speech.size(), 0.0f);
    // 0.3 s of speech, a 5 s pause, 0.5 s of speech: the check after the pause hears only the new speech.
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::None);
    }
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(check.feed(silence.data(), silence.size()), VoiceCheck::Action::None);
    }
    int checks = 0;
    for (int i = 0; i < 5; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
            check.decide(0.3f);
            ++checks;
        }
    }
    EXPECT_EQ(checks, 1);
}

namespace {

// 0.1 s of output with a 20 ms "click" (loud noise burst) at the start and silence after it.
std::vector<float> clickBlock() {
    std::vector<float> x(1600, 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (std::size_t i = 0; i < 320; ++i) {
        x[i] = u(rng);
    }
    return x;
}

std::vector<float> speechBlock() {
    std::vector<float> x(1600);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.1f * static_cast<float>(std::sin(test::kTwoPi * 300.0 * static_cast<double>(i) / 16000.0));
    }
    return x;
}

} // namespace

TEST(SpeakerCheck, ClicksAreNotSpeech) {
    VoiceCheck check;
    const std::vector<float> speech = speechBlock();
    const std::vector<float> silence(speech.size(), 0.0f);
    const std::vector<float> click = clickBlock();
    // The user talks, then stops: after 1 s the gate waits for the next speech.
    for (int i = 0; i < 5; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            check.decide(0.6f);
        }
    }
    for (int i = 0; i < 10; ++i) {
        check.feed(silence.data(), silence.size());
    }
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    // Clicking for 5 s neither opens the gate nor starts a trial, and a gate the worker opened on a click is
    // closed again by the next quiet moment.
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(check.feed(click.data(), click.size()), VoiceCheck::Action::None);
        EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
        EXPECT_TRUE(check.closeOnsetGate());
    }
    // Real speech still opens it at once.
    check.feed(speech.data(), speech.size());
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    EXPECT_FALSE(check.closeOnsetGate());
}

TEST(SpeakerCheck, ClicksDoNotKeepTheGateMuted) {
    VoiceCheck check;
    const std::vector<float> speech = speechBlock();
    const std::vector<float> click = clickBlock();
    // Someone else: muted.
    bool muted = false;
    for (int i = 0; i < 10 && !muted; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            muted = check.decide(0.05f);
        }
    }
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // They stop, but the user keeps clicking: the pause still ends their turn after 0.3 s, so the user's next
    // words pass. (Before, every click restarted the pause and the gate stayed muted.)
    const std::vector<float> silence(click.size(), 0.0f);
    check.feed(silence.data(), silence.size());
    check.feed(click.data(), click.size());
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    check.feed(click.data(), click.size());
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    EXPECT_TRUE(check.resetDue());
}

TEST(SpeakerCheck, PauseDuringTrialMutesUntilNextSpeech) {
    VoiceCheck check;
    const std::vector<float> speech = speechBlock();
    const std::vector<float> silence(speech.size(), 0.0f);
    for (int i = 0; i < 11; ++i) {
        check.feed(silence.data(), silence.size());
    }
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    // 0.2 s of speech passes at once, but cannot be judged yet ...
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::None);
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    // ... a 0.3 s pause mutes until the next speech (no more passing whatever comes meanwhile) ...
    for (int i = 0; i < 3; ++i) {
        check.feed(silence.data(), silence.size());
    }
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::OpenOnSpeech);
    EXPECT_FALSE(check.resetDue());
    // ... and the trial goes on with the next phrase: 0.2 + 0.2 s of speech is judged.
    EXPECT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::None);
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
    ASSERT_EQ(check.feed(speech.data(), speech.size()), VoiceCheck::Action::Check);
    EXPECT_EQ(check.speech().size(), 16000u * 2 / 5);
    EXPECT_TRUE(check.decide(0.05f));
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Muted);
}

TEST(SpeakerCheck, UnclearTrialReopensOnOneClearCheck) {
    VoiceCheck check;
    const std::vector<float> speech = speechBlock();
    const std::vector<float> silence(speech.size(), 0.0f);
    for (int i = 0; i < 11; ++i) {
        check.feed(silence.data(), silence.size());
    }
    // Unclear on 0.4 s and on 0.8 s: muted (fail closed) ...
    bool muted = false;
    for (int i = 0; i < 10 && !muted; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            muted = check.decide(0.18f);
        }
    }
    ASSERT_TRUE(muted);
    ASSERT_EQ(check.gate(), VoiceCheck::Gate::Muted);
    // ... but it was the user: the next clear check reopens at once.
    bool checked = false;
    for (int i = 0; i < 10 && !checked; ++i) {
        if (check.feed(speech.data(), speech.size()) == VoiceCheck::Action::Check) {
            EXPECT_FALSE(check.decide(0.3f));
            checked = true;
        }
    }
    ASSERT_TRUE(checked);
    EXPECT_EQ(check.gate(), VoiceCheck::Gate::Open);
}

TEST(SpeakerCheck, StateResetKeepsTheAudioFlowing) {
    IdentityModel model(2);
    Stream stream(2, 160);
    std::vector<float> x(4000);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.3f * static_cast<float>(std::sin(test::kTwoPi * 500.0 * static_cast<double>(i) / 16000.0));
    }
    const std::vector<float> first = run(stream, model, x);
    ASSERT_GT(model.stateSeen.size(), 3u);
    EXPECT_GT(model.stateSeen.back(), 1.0f);
    stream.resetState();
    const std::size_t calls = model.stateSeen.size();
    const std::vector<float> second = run(stream, model, x);
    ASSERT_GT(model.stateSeen.size(), calls);
    EXPECT_EQ(model.stateSeen[calls], 0.0f);
    // The audio continues without a gap (the output is sample-aligned with the input, fed twice in a row).
    std::vector<float> all = first;
    all.insert(all.end(), second.begin(), second.end());
    ASSERT_GT(all.size(), 7000u);
    for (std::size_t i = 600; i < 7000; ++i) {
        ASSERT_NEAR(all[i], x[i % x.size()], 1e-4f) << i;
    }
}

TEST(SpeakerCheck, NoiseFilterRemovesSteadyNoiseAndKeepsTheVoice) {
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.01f); // about -40 dBFS
    std::vector<float> x(16000 * 8);
    for (float& v : x) {
        v = noise(rng);
    }
    // From 5 s on, a "voice" (two harmonics, -16 dBFS) over the same noise.
    for (std::size_t i = 16000 * 5; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        x[i] += static_cast<float>(0.15 * std::sin(test::kTwoPi * 220.0 * t) + 0.08 * std::sin(test::kTwoPi * 440.0 * t));
    }
    IdentityModel plainModel(2);
    Stream plain(2, 160);
    const std::vector<float> reference = run(plain, plainModel, x);
    IdentityModel model(2);
    Stream filtered(2, 160);
    filtered.setNoiseFilter(true);
    EXPECT_TRUE(filtered.noiseFilter());
    const std::vector<float> out = run(filtered, model, x);
    ASSERT_EQ(out.size(), reference.size());

    // Noise passed through by the model, once the floor has settled (2 s): at least 10 dB quieter (measured 11).
    const double noiseBefore = rms(reference, 16000 * 2, 16000 * 5 - 1000);
    const double noiseAfter = rms(out, 16000 * 2, 16000 * 5 - 1000);
    EXPECT_LT(20.0 * std::log10(noiseAfter / noiseBefore), -10.0);
    // The voice keeps its level (within 1 dB) ...
    const std::size_t b = 16000 * 6;
    const std::size_t e = 16000 * 8 - 2000;
    for (double f : {220.0, 440.0}) {
        EXPECT_NEAR(20.0 * std::log10(toneLevel(out, b, e, f) / toneLevel(reference, b, e, f)), 0.0, 1.0) << f;
    }
    // ... while the noise between its harmonics is reduced.
    double residual = 0.0;
    double residualRef = 0.0;
    for (std::size_t i = b; i < e; ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        const double voice = 0.15 * std::sin(test::kTwoPi * 220.0 * t) + 0.08 * std::sin(test::kTwoPi * 440.0 * t);
        residual += (out[i] - voice) * (out[i] - voice);
        residualRef += (reference[i] - voice) * (reference[i] - voice);
    }
    EXPECT_LT(10.0 * std::log10(residual / residualRef), -6.0);
}
