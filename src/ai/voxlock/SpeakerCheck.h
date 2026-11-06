#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vs::voxlock {

// ---- Speaker check: is the voice that comes out of the isolation model the enrolled one? ---------------
//
// VoxLock keeps "the voice that sounds most like the sample". When the user is silent and only someone else
// talks, there is no right answer for it, and a similar voice (same gender, similar pitch) passes at full
// level; its LSTM state then stays locked on that voice, so the user is even cut when they start again.
// A speaker-verification network (CAM++, trained for exactly this question) looks at the last second or two
// of isolated speech and gives a similarity to the user's voice. VoiceCheck turns those scores into a gate:
// the isolated output is muted while it carries someone else, and the model's state is reset so it can lock
// onto the user again as soon as they speak. After a pause the gate opens on the next speech at once and judges
// it quickly, so the user's first words after someone else talked are not cut.

inline constexpr int kFbankBins = 80;
inline constexpr int kFbankFrame = 400; // 25 ms at 16 kHz
inline constexpr int kFbankShift = 160; // 10 ms

// Kaldi-compatible log mel filterbank (kaldi-native-fbank defaults with dither 0: povey window, pre-emphasis
// 0.97, DC removal, 80 bins from 20 Hz to 8 kHz, snip edges), then the mean of each bin over time removed.
// `audio16k` in [-1, 1]. Writes frames x kFbankBins values; returns the frame count (0 if under 25 ms).
int computeFbank(const float* audio16k, std::size_t count, std::vector<float>& out);

// A speaker-embedding network (see Runtime::createSpeakerModel()). Not real-time; one thread at a time.
class ISpeakerModel {
public:
    virtual ~ISpeakerModel() = default;
    // Unit-length embedding of the speech in `audio16k` (16 kHz, [-1, 1], at least ~0.5 s). False on failure.
    virtual bool embed(const float* audio16k, std::size_t count, std::vector<float>& out) = 0;
};

// The user's voice as a reference embedding: the mean of the embeddings of 1.5 s pieces of speech (pauses
// removed) spread over the whole recording, normalised. Empty if the recording has under 1.5 s of speech.
[[nodiscard]] std::vector<float> referenceEmbedding(ISpeakerModel& model, const std::vector<float>& recording16k);

// Cosine of two unit-length embeddings (0 if the sizes differ).
[[nodiscard]] float similarity(const std::vector<float>& a, const std::vector<float>& b) noexcept;

// The decision logic, fed with the isolated 16 kHz output (before the gate). Single-threaded.
class VoiceCheck {
public:
    struct Settings {
        float closeBelow = 0.15f;    // similarity under which the voice is someone else's ...
        double closeSeconds = 0.8;   // ... judged on this much recent speech. Longer windows are a little more
                                     // reliable, but still hold the user's last words when someone else starts
                                     // talking, so that voice passed for ~1.5 s (CAM++ on 0.8 s: the user ~0.6,
                                     // another voice ~0.0)
        float openAbove = 0.22f;     // similarity over which it is the user again ...
        double openSeconds = 0.8;    // ... judged on this much (talking over the other voice: muted meanwhile)
        double pauseSeconds = 0.3;   // while muted, a pause this long ends the other voice's turn; the next
                                     // speech passes at once ...
        double trialSeconds = 0.4;   // ... and is muted again if this much of it is below closeBelow; if it is
                                     // not clearly the user (under openAbove) it is judged again on openSeconds,
                                     // and muted if it is still not clearly the user then.
                                     // Once open, it is checked every checkEvery on all of its speech (at least
                                     // openSeconds) until closeSeconds of it is there.
        double rejudgeSeconds = 1.0; // while open, a silence this long ends the user's turn: whoever speaks next
                                     // is judged like new speech after a pause (trialSeconds), on their own
                                     // speech only. Without it, someone talking after the user had been silent
                                     // for a while passed until closeSeconds of their speech had been heard -
                                     // judged together with the user's last words if those were under maxAge old
                                     // - and for good if their similarity stayed at or above closeBelow.
                                     // The model state is not reset: the model still follows the user.
        int reopenChecks = 2;        // while muted (no pause), this many checks in a row above openAbove reopen
                                     // the gate: one similar-sounding window of the other voice is not enough
        double pauseStep = 0.3;      // each new speech after a pause that turned out to be the other voice again
        int maxStrikes = 3;          // makes the pause that ends a turn this much longer (up to maxStrikes times),
                                     // so someone talking with ordinary pauses between phrases does not pass
                                     // the start of every phrase
        double forgetSeconds = 3.0;  // silence this long forgets those strikes (the conversation moved on)
        double checkEvery = 0.3;     // seconds of output between checks
        double minNewSpeech = 0.25;  // a check needs at least this much speech since the previous one
        double maxAge = 4.0;         // speech older than this is not used
        double resetEvery = 0.5;     // while muted, the model state is reset again this often (it locks onto
                                     // the voice it hears first, so the user must be heard after a reset)
    };

    enum class Action { None, Check };
    // What the output gate does: pass, mute, or mute until the next speech starts and then pass.
    enum class Gate { Open, Muted, OpenOnSpeech };

    VoiceCheck();
    explicit VoiceCheck(const Settings& settings);

    void reset() noexcept; // open, nothing heard
    // Adds isolated output; returns Check when a decision is due (then call speech() and decide()).
    Action feed(const float* out16k, std::size_t count);
    // The most recent speech for the due check (pauses removed).
    [[nodiscard]] const std::vector<float>& speech() const noexcept { return window_; }
    // Applies the similarity of speech() to the user's voice. Returns true if the model state should be reset.
    bool decide(float score);
    // While muted, a periodic state reset may be due without a check (call after feed()).
    bool resetDue() noexcept;

    // True while the output passes (also while new speech after a pause is still being judged).
    [[nodiscard]] bool open() const noexcept { return state_ == State::Open || state_ == State::Trial; }
    [[nodiscard]] Gate gate() const noexcept;
    // Mean square above which output counts as speech (for the gate's own onset detection).
    [[nodiscard]] float speechLevel() const noexcept;
    [[nodiscard]] float lastScore() const noexcept { return lastScore_; }

    // Seconds of pause that currently end the other voice's turn (grows with strikes).
    [[nodiscard]] double pauseNeeded() const noexcept { return s_.pauseSeconds + strikes_ * s_.pauseStep; }

private:
    static constexpr int kBlock = 160; // 10 ms
    void mute() noexcept;
    // Whoever speaks next is judged on their own speech; `resetModel` also restarts the model state.
    void arm(bool resetModel) noexcept;
    // Muted -> (pause) -> Armed -> (speech) -> Trial -> Open or Muted; Open -> (long silence) -> Armed.
    enum class State { Open, Muted, Armed, Trial };
    Settings s_;
    State state_ = State::Open;
    bool resetPending_ = false;
    int strikes_ = 0;                   // trials in a row that were the other voice (longer pause needed)
    int reopenStreak_ = 0;              // checks in a row above openAbove while muted
    bool longTrial_ = false;            // the quick trial was ambiguous: judged again on openSeconds
    bool probation_ = false;            // open after a trial: checked on shorter windows until closeSeconds
    bool asked_ = false;                // a trial check was handed out in this feed() call
    long long silent_ = 0;              // samples since the last speech block
    float lastScore_ = 1.0f;
    float peakDb_ = -60.0f;
    long long time_ = 0;                // samples fed
    long long nextCheck_ = 0;
    long long lastReset_ = 0;
    long long newSpeech_ = 0;
    std::vector<float> partial_;        // samples of an incomplete block
    struct Block {
        long long time;
        float samples[kBlock];
    };
    std::vector<Block> blocks_;         // ring of the newest speech blocks
    std::size_t head_ = 0;              // next write position
    std::size_t count_ = 0;
    std::vector<float> window_;
};

} // namespace vs::voxlock
