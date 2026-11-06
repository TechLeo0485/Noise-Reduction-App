#pragma once

#include "audio/EngineConfig.h"
#include "core/Parameters.h"
#include "diagnostics/Logger.h"
#include "settings/Presets.h"

#include <QString>

#include <array>
#include <map>
#include <string>
#include <vector>

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
    bool closeToTray = true; // closing the window keeps VoiceStudio running in the notification area
    // The VoiceStudio Virtual Microphone was found once and VoiceStudio switched its output to it (done only once,
    // so a later manual choice of another device sticks).
    bool virtualMicAdopted = false;
    // Where voice isolation runs: "auto" (measured on this PC) or "cpu" / a GPU id from HardwareInfo.
    std::string processor = "auto";
    // Devices the user marked as a virtual microphone (their own driver, or any device the name check misses).
    std::vector<std::wstring> virtualDeviceIds;
    // System-wide shortcuts (formatHotkey() text, "" = off). Three modifiers, so they clash with few other apps.
    static constexpr const char* kDefaultHotkeyShowWindow = "Ctrl+Alt+Shift+V";
    static constexpr const char* kDefaultHotkeyMute = "Ctrl+Alt+Shift+M";
    std::string hotkeyShowWindow = kDefaultHotkeyShowWindow;
    std::string hotkeyMute = kDefaultHotkeyMute;
    // Voice shortcuts: switch to the next voice in the list; turn the voice effects off (natural voice) and back on.
    static constexpr const char* kDefaultHotkeyNextVoice = "Ctrl+Alt+Shift+N";
    static constexpr const char* kDefaultHotkeyVoiceOnOff = "Ctrl+Alt+Shift+B";
    std::string hotkeyNextVoice = kDefaultHotkeyNextVoice;
    std::string hotkeyVoiceOnOff = kDefaultHotkeyVoiceOnOff;
    // Voices the user saved under their own names (after the built-in looks), and the voice that "voice effects
    // on" returns to after they were turned off.
    std::vector<CustomVoice> customVoices;
    VoiceValues lastVoice{-1.5f, 1.5f, 3.f};
    // The voice-isolation model (IsolationModels.h).
    std::string isolationModel = "voxlock";
    // Updates (GitHub releases): check when VoiceStudio starts; a version the user chose to skip.
    bool checkForUpdates = true;
    std::string skippedVersion;
    // The version that last ran ("" = none yet: a fresh install). When the running version is newer, VoiceStudio
    // shows what changed: `updateNotes` (saved before the update was installed, for `updateNotesVersion`) or the
    // release notes fetched from GitHub.
    std::string lastRunVersion;
    std::string updateNotesVersion;
    std::string updateNotes;

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
