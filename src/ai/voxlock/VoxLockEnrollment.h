#pragma once

#include <filesystem>
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

// The enrolled voice sample on disk: 16 kHz mono float32 WAV, written atomically (temp file + rename).
bool saveVoiceSample(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error);
bool loadVoiceSample(const std::filesystem::path& path, std::vector<float>& audio16k, std::string& error);

// The full recording the sample came from, kept for the user: 16 kHz mono 16-bit PCM WAV (atomic write).
bool saveRecording(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error);

} // namespace vs::voxlock
