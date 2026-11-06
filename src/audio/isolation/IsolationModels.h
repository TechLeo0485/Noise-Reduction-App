#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vs {

// The voice-isolation models the user can choose from ("Only my voice" page). Each one keeps only the enrolled
// voice; they differ in how well they separate voices that overlap, in delay and in CPU cost. All of them use the
// same voice recording (each builds what it needs from it), the voice check (CAM++) and the noise floor filter, so
// switching models never needs a new recording.
//
// Adding a model: add an entry here, then teach VoiceIsolator::calibrateAndPublish() to build its chunk model
// (voxlock::IChunkModel) for the selected id.
struct IsolationModelInfo {
    std::string id;           // stored in the settings ("isolationModel")
    std::string name;         // plain name for the UI, e.g. "Full isolation"
    std::string engine;       // the technology behind it, e.g. "VoxLock neural network"
    std::string howItWorks;   // one or two sentences, no jargon
    std::string useWhen;      // when to pick it
    std::string badge;        // short tag on the tile, e.g. "RECOMMENDED"
    std::string delay;        // typical added delay, for the tile
    std::string cpu;          // typical CPU cost, for the tile
    std::string overlapping;  // what happens when someone talks at the same time as the user
    std::vector<std::string> files; // model files it needs, relative to the models folder (next to the exe)
    std::string installHint;  // shown when a file is missing
};

inline constexpr const char* kDefaultIsolationModel = "voxlock";

// In the order the UI shows them; the first is the default.
[[nodiscard]] const std::vector<IsolationModelInfo>& isolationModels();
[[nodiscard]] const IsolationModelInfo* findIsolationModel(std::string_view id);
// All of the model's files are in `modelsDir` (the "models" folder next to the exe).
[[nodiscard]] bool isolationModelInstalled(const IsolationModelInfo& model, const std::filesystem::path& modelsDir);

} // namespace vs
