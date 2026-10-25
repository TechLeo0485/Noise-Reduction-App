#pragma once

#include "audio/EngineConfig.h"
#include "core/Parameters.h"
#include "diagnostics/Logger.h"

#include <QString>

#include <array>
#include <map>
#include <string>

namespace vs {

// Everything persisted between runs. Plain data; validated on load.
struct AppSettings {
    static constexpr int kCurrentVersion = 1;

    int version = kCurrentVersion;
    EngineConfig engine{};
    std::array<float, kParamCount> params{};
    EffectOrder effectOrder = defaultEffectOrder();
    std::string activePreset = "Natural Voice";
    std::string performanceMode = "balanced";
    std::map<std::string, std::map<std::string, float>> customPresets;
    LogLevel logLevel = LogLevel::Info;
    bool engineEnabled = true;
    bool advancedUi = false; // simple UI by default; technical screens are opt-in

    AppSettings();
    void captureParams(const EngineParameters& source);
    void applyParams(EngineParameters& target) const;
};

// Versioned JSON persistence (%LOCALAPPDATA%\VoiceStudio\settings.json).
// Writes are atomic (QSaveFile) so a crash during save cannot corrupt the file.
class SettingsStore {
public:
    [[nodiscard]] static QString defaultPath();

    // Missing file -> defaults (returns true). Corrupt/unsupported file -> defaults, returns
    // false with `error` set; the bad file is kept as settings.json.bad for diagnosis.
    static bool load(const QString& path, AppSettings& out, QString* error = nullptr);
    static bool save(const QString& path, const AppSettings& settings, QString* error = nullptr);

    // Serialisation helpers (exposed for tests).
    [[nodiscard]] static QByteArray toJson(const AppSettings& settings);
    static bool fromJson(const QByteArray& json, AppSettings& out, QString* error = nullptr);
};

} // namespace vs
