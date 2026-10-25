#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

namespace {

using K = ParamKind;

constexpr std::array<ParamSpec, kParamCount> kSpecs{{
    {ParamId::InputGainDb, "inputGainDb", "Microphone gain", K::Float, -24.f, 24.f, 0.f, 0.5f, "dB"},
    {ParamId::VoiceVolumeDb, "voiceVolumeDb", "Voice volume", K::Float, -24.f, 12.f, 0.f, 0.5f, "dB"},
    {ParamId::MasterMute, "masterMute", "Master mute", K::Bool, 0.f, 1.f, 0.f, 1.f, ""},
    {ParamId::MonitorGainDb, "monitorGainDb", "Monitor volume", K::Float, -40.f, 6.f, -6.f, 0.5f, "dB"},

    {ParamId::SuppressionEnabled, "suppressionEnabled", "Noise suppression", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},
    {ParamId::SuppressionMode, "suppressionMode", "Suppression mode", K::Choice, 0.f, 2.f, 1.f, 1.f, ""},
    {ParamId::SuppressionStrength, "suppressionStrength", "Suppression strength", K::Float, 0.f, 100.f, 100.f, 1.f, "%"},

    {ParamId::HighPassEnabled, "highPassEnabled", "High-pass filter", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},
    {ParamId::HighPassHz, "highPassHz", "High-pass cutoff", K::Float, 20.f, 300.f, 80.f, 1.f, "Hz"},

    {ParamId::GateEnabled, "gateEnabled", "Noise gate", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},
    {ParamId::GateThresholdDb, "gateThresholdDb", "Gate threshold", K::Float, -80.f, -10.f, -50.f, 0.5f, "dBFS"},
    {ParamId::GateRangeDb, "gateRangeDb", "Gate range", K::Float, 0.f, 80.f, 30.f, 1.f, "dB"},
    {ParamId::GateRatio, "gateRatio", "Expander ratio", K::Float, 1.f, 20.f, 4.f, 0.1f, ":1"},
    {ParamId::GateAttackMs, "gateAttackMs", "Gate attack", K::Float, 0.1f, 50.f, 2.f, 0.1f, "ms"},
    {ParamId::GateHoldMs, "gateHoldMs", "Gate hold", K::Float, 0.f, 500.f, 80.f, 1.f, "ms"},
    {ParamId::GateReleaseMs, "gateReleaseMs", "Gate release", K::Float, 10.f, 1000.f, 150.f, 1.f, "ms"},
    {ParamId::GateUseVad, "gateUseVad", "Keep gate open on detected voice", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},

    {ParamId::BassDb, "bassDb", "Bass", K::Float, -12.f, 12.f, 0.f, 0.5f, "dB"},
    {ParamId::MidDb, "midDb", "Mid", K::Float, -12.f, 12.f, 0.f, 0.5f, "dB"},
    {ParamId::TrebleDb, "trebleDb", "Treble", K::Float, -12.f, 12.f, 0.f, 0.5f, "dB"},
    {ParamId::WarmthDb, "warmthDb", "Warmth", K::Float, -12.f, 12.f, 0.f, 0.5f, "dB"},
    {ParamId::ClarityDb, "clarityDb", "Clarity", K::Float, -12.f, 12.f, 0.f, 0.5f, "dB"},

    {ParamId::CompressorEnabled, "compressorEnabled", "Compressor", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},
    {ParamId::CompThresholdDb, "compThresholdDb", "Compressor threshold", K::Float, -60.f, 0.f, -20.f, 0.5f, "dBFS"},
    {ParamId::CompRatio, "compRatio", "Compressor ratio", K::Float, 1.f, 20.f, 2.5f, 0.1f, ":1"},
    {ParamId::CompAttackMs, "compAttackMs", "Compressor attack", K::Float, 0.1f, 100.f, 8.f, 0.1f, "ms"},
    {ParamId::CompReleaseMs, "compReleaseMs", "Compressor release", K::Float, 10.f, 1000.f, 150.f, 1.f, "ms"},
    {ParamId::CompMakeupDb, "compMakeupDb", "Makeup gain", K::Float, 0.f, 24.f, 3.f, 0.5f, "dB"},

    {ParamId::LimiterEnabled, "limiterEnabled", "Limiter", K::Bool, 0.f, 1.f, 1.f, 1.f, ""},
    {ParamId::LimiterCeilingDb, "limiterCeilingDb", "Limiter ceiling", K::Float, -12.f, 0.f, -1.f, 0.1f, "dBFS"},
    {ParamId::LimiterReleaseMs, "limiterReleaseMs", "Limiter release", K::Float, 5.f, 500.f, 60.f, 1.f, "ms"},

    {ParamId::PitchEnabled, "pitchEnabled", "Pitch shift", K::Bool, 0.f, 1.f, 0.f, 1.f, ""},
    {ParamId::PitchSemitones, "pitchSemitones", "Pitch", K::Float, -12.f, 12.f, 0.f, 0.5f, "st"},

    {ParamId::IsolationEnabled, "isolationEnabled", "Strict voice isolation", K::Bool, 0.f, 1.f, 0.f, 1.f, ""},
}};

constexpr bool specsAreOrdered() {
    for (std::size_t i = 0; i < kSpecs.size(); ++i) {
        if (static_cast<std::size_t>(kSpecs[i].id) != i) {
            return false;
        }
    }
    return true;
}
static_assert(specsAreOrdered(), "kSpecs must be listed in ParamId order");

constexpr std::array<const char*, kEffectSlotCount> kEffectKeys{"eq", "compressor", "pitch"};

std::uint32_t packOrder(const EffectOrder& order) noexcept {
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        packed |= static_cast<std::uint32_t>(order[i]) << (i * 4);
    }
    return packed;
}

EffectOrder unpackOrder(std::uint32_t packed) noexcept {
    EffectOrder order{};
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<EffectSlot>((packed >> (i * 4)) & 0xF);
    }
    return order;
}

} // namespace

const std::array<ParamSpec, kParamCount>& paramSpecs() noexcept {
    return kSpecs;
}

const ParamSpec& paramSpec(ParamId id) noexcept {
    return kSpecs[static_cast<std::size_t>(id)];
}

const ParamSpec* findParam(std::string_view key) noexcept {
    for (const auto& spec : kSpecs) {
        if (key == spec.key) {
            return &spec;
        }
    }
    return nullptr;
}

float clampParam(ParamId id, float value) noexcept {
    const ParamSpec& spec = paramSpec(id);
    if (!std::isfinite(value)) {
        return spec.defaultValue;
    }
    value = std::clamp(value, spec.minValue, spec.maxValue);
    if (spec.kind == ParamKind::Bool) {
        value = value >= 0.5f ? 1.0f : 0.0f;
    } else if (spec.kind == ParamKind::Choice) {
        value = std::round(value);
    }
    return value;
}

EffectOrder defaultEffectOrder() noexcept {
    return {EffectSlot::Equalizer, EffectSlot::Compressor, EffectSlot::Pitch};
}

bool isValidEffectOrder(const EffectOrder& order) noexcept {
    std::array<bool, kEffectSlotCount> seen{};
    for (EffectSlot slot : order) {
        const auto idx = static_cast<std::size_t>(slot);
        if (idx >= kEffectSlotCount || seen[idx]) {
            return false;
        }
        seen[idx] = true;
    }
    return true;
}

const char* effectSlotKey(EffectSlot slot) noexcept {
    const auto idx = static_cast<std::size_t>(slot);
    return idx < kEffectKeys.size() ? kEffectKeys[idx] : "";
}

bool effectSlotFromKey(std::string_view key, EffectSlot& out) noexcept {
    for (std::size_t i = 0; i < kEffectKeys.size(); ++i) {
        if (key == kEffectKeys[i]) {
            out = static_cast<EffectSlot>(i);
            return true;
        }
    }
    return false;
}

EngineParameters::EngineParameters() noexcept : packedEffectOrder_(packOrder(defaultEffectOrder())) {
    resetToDefaults();
}

float EngineParameters::set(ParamId id, float value) noexcept {
    const float clamped = clampParam(id, value);
    values_[static_cast<std::size_t>(id)].store(clamped, std::memory_order_relaxed);
    return clamped;
}

void EngineParameters::resetToDefaults() noexcept {
    for (const auto& spec : kSpecs) {
        values_[static_cast<std::size_t>(spec.id)].store(spec.defaultValue, std::memory_order_relaxed);
    }
    packedEffectOrder_.store(packOrder(defaultEffectOrder()), std::memory_order_relaxed);
}

void EngineParameters::setEffectOrder(const EffectOrder& order) noexcept {
    if (isValidEffectOrder(order)) {
        packedEffectOrder_.store(packOrder(order), std::memory_order_relaxed);
    }
}

EffectOrder EngineParameters::effectOrder() const noexcept {
    const EffectOrder order = unpackOrder(packedEffectOrder_.load(std::memory_order_relaxed));
    return isValidEffectOrder(order) ? order : defaultEffectOrder();
}

} // namespace vs
