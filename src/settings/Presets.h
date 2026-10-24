#pragma once

#include "audio/wasapi/WasapiCommon.h"
#include "core/Parameters.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vs {

using ParamValues = std::vector<std::pair<ParamId, float>>;

struct Preset {
    std::string name;
    std::string description;
    ParamValues values; // overrides on top of defaults for preset-scoped parameters
};

// Parameters a preset controls. Microphone gain, master mute and monitor volume are
// per-setup values and are deliberately left untouched when switching presets.
[[nodiscard]] bool isPresetScoped(ParamId id) noexcept;

[[nodiscard]] const std::vector<Preset>& builtInPresets();
[[nodiscard]] const Preset* findBuiltInPreset(const std::string& name);

// Resets preset-scoped parameters to defaults, then applies the preset's values.
void applyPreset(const Preset& preset, EngineParameters& params);

// Captures the current preset-scoped values (used to save a custom preset).
[[nodiscard]] std::map<std::string, float> capturePresetValues(const EngineParameters& params);
void applyPresetValues(const std::map<std::string, float>& values, EngineParameters& params);

enum class PerformanceMode : int { UltraLowLatency = 0, Balanced = 1, MaximumNoiseReduction = 2, Custom = 3 };

struct PerformanceProfile {
    PerformanceMode mode;
    const char* key;
    const char* label;
    const char* description;
    BufferMode bufferMode;
    double outputTargetMs;
    ParamValues values;
};

[[nodiscard]] const std::vector<PerformanceProfile>& performanceProfiles();
[[nodiscard]] const PerformanceProfile* findPerformanceProfile(const std::string& key);

} // namespace vs
