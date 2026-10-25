#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vs::voxlock {

// voxlock.core.loud_sample: the loudest `seconds` (in 10 ms steps) within the first `withinSeconds`.
[[nodiscard]] std::vector<float> loudSample(const std::vector<float>& audio16k, double seconds, double withinSeconds);

// The most representative `seconds` of a recording of any length (in 10 ms steps): the window with the most
// speech, at a level close to the speaker's typical level, avoiding clipped passages. A long recording with
// loud, quiet, high and low passages therefore gives a sample of the normal speaking voice, not a shout.
[[nodiscard]] std::vector<float> representativeSample(const std::vector<float>& audio16k, double seconds);

// The voice sample as the network takes it, exactly 3 s (see representativeSample()). A sample of exactly
// 3 s is used as is. Returns false (and an empty vector) if shorter than 3 s.
bool fitSample(const std::vector<float>& audio16k, std::vector<float>& out, std::string& error);

// Checks that a recording is usable as a voice sample: at least 3 s, the loudest 3 s clearly above the
// noise floor and not clipped. `error` is a user-facing explanation.
bool validateVoiceSample(const std::vector<float>& audio16k, std::string& error);

// ---- Choosing the voice sample from a long, varied recording ------------------------------------------
//
// The network only takes exactly 3 s (its attention is built for that length), so a long recording with loud,
// quiet, high and low speech cannot be passed in whole. Instead the recording is split into sections (the guide
// runs through the styles in order, so sections roughly follow them) and several 3 s candidates are built: the
// most typical window overall, the most typical window of each section, and a mix of 1 s pieces from different
// sections. Each candidate is then tested on held-out speech from every section, and the one that keeps the
// most of the speaker across all of them - judged on the worst section as much as on the average - is used.

struct SampleRange {
    std::size_t begin = 0; // samples
    std::size_t end = 0;
};

struct SampleCandidate {
    std::vector<float> sample;        // exactly kEnrollSamples
    std::vector<SampleRange> sources; // where its audio comes from in the recording
    std::string label;                // for the log, e.g. "section 3" or "mix"
};

struct EnrollmentPlan {
    std::vector<SampleCandidate> candidates; // the first is the most typical 3 s overall (what fitSample() gives)
    std::vector<SampleRange> tests;          // held-out speech, one per section
};

// Candidates and test segments for `audio16k` (at least 3 s). Short recordings (under about 12 s) give a
// single candidate and no tests: there is nothing to choose between.
[[nodiscard]] EnrollmentPlan planEnrollment(const std::vector<float>& audio16k, int maxSections = 6);

// How much of the speech in `input` survives in `output` (sample-aligned), in dB: 0 = all of it, negative = cut.
// Measured over the speech blocks of the input only, so pauses do not count. Clamped to [-40, +6].
[[nodiscard]] double keptDb(const float* input, const float* output, std::size_t count);

// Runs the candidates of `plan` through `isolate` and returns the index of the best one: highest mean + worst
// kept level (capped at 0 dB) over the test segments it was not built from. The first candidate wins unless
// another scores at least 1 dB higher. `isolate` gets one candidate's sample and all its test
// inputs (built once per candidate, which is the expensive part) and fills one output per input, sample-aligned;
// false on failure. Returns 0 when there is nothing to choose or every candidate failed. `progress` (optional) is
// called with (first candidate index, candidate count) before each batch; `scoresOut` (optional) receives the
// scores. Up to `parallel` candidates are tested at the same time (`isolate` must then be thread-safe).
using IsolateFn = std::function<bool(const std::vector<float>& sample, const std::vector<std::vector<float>>& inputs,
                                     std::vector<std::vector<float>>& outputs)>;
[[nodiscard]] std::size_t chooseCandidate(const std::vector<float>& audio16k, const EnrollmentPlan& plan,
                                          const IsolateFn& isolate,
                                          const std::function<void(std::size_t, std::size_t)>& progress = {},
                                          std::vector<double>* scoresOut = nullptr, int parallel = 1);

// The enrolled voice sample on disk: 16 kHz mono float32 WAV, written atomically (temp file + rename).
bool saveVoiceSample(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error);
// Reads a 16 kHz mono WAV: float32 (the voice sample) or 16-bit PCM (the full recording, see saveRecording()).
bool loadVoiceSample(const std::filesystem::path& path, std::vector<float>& audio16k, std::string& error);

// The full recording the sample came from, kept for the user: 16 kHz mono 16-bit PCM WAV (atomic write).
bool saveRecording(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error);

} // namespace vs::voxlock
