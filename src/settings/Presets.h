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

// Parameters a preset controls. Microphone gain, master mute, monitor volume, strict voice isolation
// and the voice character are per-user values and are deliberately left untouched when switching presets.
[[nodiscard]] bool isPresetScoped(ParamId id) noexcept;

[[nodiscard]] constexpr bool isVoiceCharacterParam(ParamId id) noexcept {
    return id == ParamId::PitchEnabled || id == ParamId::PitchSemitones || id == ParamId::PitchBodySt ||
           id == ParamId::PitchPresenceDb;
}

// A ready-made voice character ("look"): depth, body and presence. Applied on top of any preset.
struct VoiceLook {
    const char* key;
    const char* label;
    const char* description;
    float depthSt;
    float bodySt;
    float presenceDb;
};

// "natural" first (character off); the others switch it on.
[[nodiscard]] const std::vector<VoiceLook>& voiceLooks();
// Sets the voice-character parameters to the look (Natural switches the character off).
void applyVoiceLook(const VoiceLook& look, EngineParameters& params);
// The look the current values match, or nullptr (custom).
[[nodiscard]] const VoiceLook* matchVoiceLook(const EngineParameters& params);

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
