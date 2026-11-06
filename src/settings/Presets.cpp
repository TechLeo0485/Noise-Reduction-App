#include "settings/Presets.h"

#include <cmath>

namespace vs {

namespace {
constexpr float kRNNoise = static_cast<float>(SuppressionMode::RNNoise);
constexpr float kDspOnly = static_cast<float>(SuppressionMode::DspOnly);
} // namespace

bool isPresetScoped(ParamId id) noexcept {
    // Strict isolation is a privacy control, not a sound setting: a preset must never switch it off.
    // The voice character (depth, body, presence) is the user's own voice and has its own looks, so
    // switching between room/noise presets keeps it. The voice stabilizer is a general choice, not a sound.
    return id != ParamId::InputGainDb && id != ParamId::MasterMute && id != ParamId::MonitorGainDb &&
           id != ParamId::IsolationEnabled && id != ParamId::StabilizerEnabled && !isVoiceCharacterParam(id);
}

const std::vector<Preset>& builtInPresets() {
    static const std::vector<Preset> presets = {
        {"Interview",
         "Natural, intelligible speech with moderate dynamics control and low latency.",
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 90.f},
          {ParamId::HighPassHz, 80.f},
          {ParamId::GateThresholdDb, -55.f},
          {ParamId::GateRangeDb, 18.f},
          {ParamId::GateRatio, 2.5f},
          {ParamId::GateReleaseMs, 220.f},
          {ParamId::ClarityDb, 2.0f},
          {ParamId::WarmthDb, 1.0f},
          {ParamId::CompressorEnabled, 1.f},
          {ParamId::CompThresholdDb, -22.f},
          {ParamId::CompRatio, 2.5f},
          {ParamId::CompMakeupDb, 3.f}}},
        {"Natural Voice",
         "Minimal colouration: noise suppression and safety limiting only.",
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 85.f},
          {ParamId::GateRangeDb, 15.f},
          {ParamId::GateRatio, 2.f},
          {ParamId::CompressorEnabled, 0.f}}},
        {"Gaming",
         "Aggressive keyboard/mouse suppression and a tighter gate.",
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 100.f},
          {ParamId::HighPassHz, 100.f},
          {ParamId::GateThresholdDb, -45.f},
          {ParamId::GateRangeDb, 40.f},
          {ParamId::GateRatio, 6.f},
          {ParamId::GateReleaseMs, 120.f},
          {ParamId::ClarityDb, 3.f},
          {ParamId::CompressorEnabled, 1.f},
          {ParamId::CompThresholdDb, -24.f},
          {ParamId::CompRatio, 3.5f},
          {ParamId::CompMakeupDb, 4.f}}},
        {"Streaming",
         "Broadcast-style voice: warmer, more present, more compressed.",
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 95.f},
          {ParamId::BassDb, 2.f},
          {ParamId::WarmthDb, 2.f},
          {ParamId::ClarityDb, 3.f},
          {ParamId::TrebleDb, 1.5f},
          {ParamId::CompressorEnabled, 1.f},
          {ParamId::CompThresholdDb, -26.f},
          {ParamId::CompRatio, 4.f},
          {ParamId::CompMakeupDb, 6.f},
          {ParamId::LimiterCeilingDb, -1.f}}},
        {"Noise Reduction Maximum",
         "Strongest suppression and gating. May sound less natural; use in very noisy rooms.",
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 100.f},
          {ParamId::HighPassHz, 120.f},
          {ParamId::GateThresholdDb, -42.f},
          {ParamId::GateRangeDb, 60.f},
          {ParamId::GateRatio, 10.f},
          {ParamId::GateHoldMs, 120.f},
          {ParamId::CompressorEnabled, 1.f}}},
        {"Low CPU",
         "DSP-only noise control for very old or heavily loaded machines.",
         {{ParamId::SuppressionMode, kDspOnly},
          {ParamId::SuppressionStrength, 100.f},
          {ParamId::GateRangeDb, 30.f},
          {ParamId::CompressorEnabled, 0.f}}},
    };
    return presets;
}

const Preset* findBuiltInPreset(const std::string& name) {
    for (const Preset& p : builtInPresets()) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

void applyPreset(const Preset& preset, EngineParameters& params) {
    for (const ParamSpec& spec : paramSpecs()) {
        if (isPresetScoped(spec.id)) {
            params.set(spec.id, spec.defaultValue);
        }
    }
    for (const auto& [id, value] : preset.values) {
        if (isPresetScoped(id)) {
            params.set(id, value);
        }
    }
}

std::map<std::string, float> capturePresetValues(const EngineParameters& params) {
    std::map<std::string, float> values;
    for (const ParamSpec& spec : paramSpecs()) {
        if (isPresetScoped(spec.id)) {
            values[spec.key] = params.get(spec.id);
        }
    }
    return values;
}

void applyPresetValues(const std::map<std::string, float>& values, EngineParameters& params) {
    for (const ParamSpec& spec : paramSpecs()) {
        if (isPresetScoped(spec.id)) {
            params.set(spec.id, spec.defaultValue);
        }
    }
    for (const auto& [key, value] : values) {
        const ParamSpec* spec = findParam(key);
        if (spec != nullptr && isPresetScoped(spec->id)) {
            params.set(spec->id, value); // clamped by EngineParameters::set
        }
    }
}

const std::vector<VoiceLook>& voiceLooks() {
    static const std::vector<VoiceLook> looks = {
        {"natural", "Natural", "Your own voice, unchanged.", 0.f, 0.f, 0.f},
        {"strongThick", "Strong & Thick", "Fuller and more solid, a touch lower - not bassy.", -1.5f, 1.5f, 3.f},
        {"deep", "Deep", "Clearly lower, still clear.", -3.f, 1.f, 4.f},
        {"warm", "Warm", "A little deeper and softer.", -1.f, 1.f, 1.5f},
        {"radio", "Radio", "Thick, close and present, like a radio host.", -1.f, 2.f, 5.f},
        {"clear", "Clear", "Same pitch, crisper and easier to understand.", 0.f, 0.5f, 6.f},
        {"lighter", "Lighter", "A little higher and lighter, still natural.", 2.f, -1.5f, 2.f},
        {"veryDeep", "Very Deep", "Much lower and bigger, with the bass kept in check.", -5.f, 2.f, 5.f},
    };
    return looks;
}

bool VoiceValues::natural() const noexcept {
    return std::fabs(depthSt) < 0.01f && std::fabs(bodySt) < 0.01f && presenceDb < 0.05f;
}

bool VoiceValues::matches(const VoiceValues& other) const noexcept {
    return std::fabs(depthSt - other.depthSt) < 0.01f && std::fabs(bodySt - other.bodySt) < 0.01f &&
           std::fabs(presenceDb - other.presenceDb) < 0.01f;
}

VoiceValues voiceValues(const VoiceLook& look) noexcept {
    return {look.depthSt, look.bodySt, look.presenceDb};
}

VoiceValues currentVoiceValues(const EngineParameters& params) {
    if (!params.getBool(ParamId::PitchEnabled)) {
        return {};
    }
    return {params.get(ParamId::PitchSemitones), params.get(ParamId::PitchBodySt), params.get(ParamId::PitchPresenceDb)};
}

void applyVoiceValues(const VoiceValues& values, EngineParameters& params) {
    params.set(ParamId::PitchSemitones, values.depthSt);
    params.set(ParamId::PitchBodySt, values.bodySt);
    params.set(ParamId::PitchPresenceDb, values.presenceDb);
    params.set(ParamId::PitchEnabled, values.natural() ? 0.f : 1.f);
}

void applyVoiceLook(const VoiceLook& look, EngineParameters& params) {
    applyVoiceValues(voiceValues(look), params);
}

const VoiceLook* matchVoiceLook(const EngineParameters& params) {
    const auto& looks = voiceLooks();
    if (!params.getBool(ParamId::PitchEnabled)) {
        return &looks.front();
    }
    for (const VoiceLook& look : looks) {
        if (std::fabs(params.get(ParamId::PitchSemitones) - look.depthSt) < 0.01f &&
            std::fabs(params.get(ParamId::PitchBodySt) - look.bodySt) < 0.01f &&
            std::fabs(params.get(ParamId::PitchPresenceDb) - look.presenceDb) < 0.01f) {
            return &look;
        }
    }
    return nullptr;
}

const std::vector<PerformanceProfile>& performanceProfiles() {
    static const std::vector<PerformanceProfile> profiles = {
        {PerformanceMode::UltraLowLatency, "ultraLowLatency", "Ultra-Low Latency",
         "Smallest device buffers (IAudioClient3 where supported), minimal jitter buffer, RNNoise, "
         "no compressor or pitch. Most sensitive to CPU spikes.",
         BufferMode::LowLatency, 6.0,
         {{ParamId::SuppressionMode, kRNNoise}, {ParamId::CompressorEnabled, 0.f}, {ParamId::PitchEnabled, 0.f}}},
        {PerformanceMode::Balanced, "balanced", "Balanced",
         "Default engine period, RNNoise, gate and light compression. Recommended.",
         BufferMode::Default, 12.0,
         {{ParamId::SuppressionMode, kRNNoise}, {ParamId::GateEnabled, 1.f}, {ParamId::CompressorEnabled, 1.f}}},
        {PerformanceMode::MaximumNoiseReduction, "maximumNoiseReduction", "Maximum Noise Reduction",
         "Full-strength suppression and a deeper gate with a larger safety buffer (+latency). "
         "The Advanced AI engine is not part of this build, so RNNoise is used.",
         BufferMode::Default, 20.0,
         {{ParamId::SuppressionMode, kRNNoise},
          {ParamId::SuppressionStrength, 100.f},
          {ParamId::GateRangeDb, 50.f},
          {ParamId::GateRatio, 8.f}}},
        {PerformanceMode::Custom, "custom", "Custom", "Manual control of every setting.", BufferMode::Default, 12.0, {}},
    };
    return profiles;
}

const PerformanceProfile* findPerformanceProfile(const std::string& key) {
    for (const PerformanceProfile& p : performanceProfiles()) {
        if (key == p.key) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace vs
