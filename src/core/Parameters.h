#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vs {

// Every user-adjustable processing parameter. The UI, presets, settings persistence and the
// real-time chain all address parameters through this single registry.
enum class ParamId : std::uint16_t {
    InputGainDb,
    VoiceVolumeDb,
    MasterMute,
    MonitorGainDb,

    SuppressionEnabled,
    SuppressionMode,
    SuppressionStrength,

    HighPassEnabled,
    HighPassHz,

    GateEnabled,
    GateThresholdDb,
    GateRangeDb,
    GateRatio,
    GateAttackMs,
    GateHoldMs,
    GateReleaseMs,
    GateUseVad,

    BassDb,
    MidDb,
    TrebleDb,
    WarmthDb,
    ClarityDb,

    CompressorEnabled,
    CompThresholdDb,
    CompRatio,
    CompAttackMs,
    CompReleaseMs,
    CompMakeupDb,

    LimiterEnabled,
    LimiterCeilingDb,
    LimiterReleaseMs,

    PitchEnabled,
    PitchSemitones,

    // Strict voice isolation (VoxLock): only the enrolled voice passes; silence otherwise.
    IsolationEnabled,

    Count
};

inline constexpr std::size_t kParamCount = static_cast<std::size_t>(ParamId::Count);

enum class ParamKind : std::uint8_t { Float, Bool, Choice };

// Values for ParamId::SuppressionMode.
enum class SuppressionMode : int { DspOnly = 0, RNNoise = 1, AdvancedAI = 2 };

struct ParamSpec {
    ParamId id;
    const char* key;   // stable identifier used in settings files and presets
    const char* label; // UI label
    ParamKind kind;
    float minValue;
    float maxValue;
    float defaultValue;
    float step;
    const char* unit;
};

[[nodiscard]] const std::array<ParamSpec, kParamCount>& paramSpecs() noexcept;
[[nodiscard]] const ParamSpec& paramSpec(ParamId id) noexcept;
[[nodiscard]] const ParamSpec* findParam(std::string_view key) noexcept;
[[nodiscard]] float clampParam(ParamId id, float value) noexcept;

// Order of the configurable voice-effect slots in the processing chain.
enum class EffectSlot : std::uint8_t { Equalizer = 0, Compressor = 1, Pitch = 2, Count = 3 };
inline constexpr std::size_t kEffectSlotCount = static_cast<std::size_t>(EffectSlot::Count);
using EffectOrder = std::array<EffectSlot, kEffectSlotCount>;

[[nodiscard]] EffectOrder defaultEffectOrder() noexcept;
[[nodiscard]] bool isValidEffectOrder(const EffectOrder& order) noexcept;
[[nodiscard]] const char* effectSlotKey(EffectSlot slot) noexcept;
[[nodiscard]] bool effectSlotFromKey(std::string_view key, EffectSlot& out) noexcept;

// Thread-safe parameter store. Writers (UI / control thread) call set(); the real-time thread
// reads with relaxed atomic loads. No locks, no allocation.
class EngineParameters {
public:
    EngineParameters() noexcept;

    [[nodiscard]] float get(ParamId id) const noexcept {
        return values_[static_cast<std::size_t>(id)].load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool getBool(ParamId id) const noexcept { return get(id) >= 0.5f; }
    [[nodiscard]] int getInt(ParamId id) const noexcept {
        return static_cast<int>(get(id) + 0.5f);
    }

    // Clamps to the registered range. Returns the stored value.
    float set(ParamId id, float value) noexcept;
    void resetToDefaults() noexcept;

    void setEffectOrder(const EffectOrder& order) noexcept;
    [[nodiscard]] EffectOrder effectOrder() const noexcept;

private:
    std::array<std::atomic<float>, kParamCount> values_;
    std::atomic<std::uint32_t> packedEffectOrder_;
};

} // namespace vs
