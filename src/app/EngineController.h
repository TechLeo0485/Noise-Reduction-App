#pragma once

#include "audio/EngineSupervisor.h"
#include "audio/monitoring/StatsCollector.h"
#include "core/Parameters.h"
#include "devices/DeviceManager.h"
#include "devices/VirtualMicDriver.h"
#include "diagnostics/ProcessMetrics.h"
#include "hardware/HardwareInfo.h"
#include "settings/SettingsStore.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace vs {

// Bridge between QML and the native engine. Lives on the UI thread.
//
// The UI never touches audio threads: parameter changes are atomic stores, meters/statistics
// are atomic loads on timers, and device/stream changes are posted to EngineSupervisor.
class EngineController : public QObject {
    Q_OBJECT

    // Devices and routing
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QString inputDeviceId READ inputDeviceId NOTIFY configChanged)
    Q_PROPERTY(bool inputFollowDefault READ inputFollowDefault WRITE setInputFollowDefault NOTIFY configChanged)
    Q_PROPERTY(QString outputDeviceId READ outputDeviceId NOTIFY configChanged)
    Q_PROPERTY(QString monitorDeviceId READ monitorDeviceId NOTIFY configChanged)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled WRITE setMonitorEnabled NOTIFY configChanged)
    Q_PROPERTY(bool cableDetected READ cableDetected NOTIFY devicesChanged)
    Q_PROPERTY(QString routingWarning READ routingWarning NOTIFY configChanged)

    // Engine configuration
    Q_PROPERTY(bool engineEnabled READ engineEnabled WRITE setEngineEnabled NOTIFY configChanged)
    Q_PROPERTY(int bufferMode READ bufferMode WRITE setBufferMode NOTIFY configChanged)
    Q_PROPERTY(double outputTargetMs READ outputTargetMs WRITE setOutputTargetMs NOTIFY configChanged)
    Q_PROPERTY(bool autoRecover READ autoRecover WRITE setAutoRecover NOTIFY configChanged)
    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel NOTIFY configChanged)
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(QString performanceMode READ performanceMode NOTIFY configChanged)
    Q_PROPERTY(QVariantList performanceModes READ performanceModes CONSTANT)
    Q_PROPERTY(QString selectedBackend READ selectedBackend WRITE setSelectedBackend NOTIFY configChanged)

    // Processing parameters and presets
    Q_PROPERTY(QVariantMap params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QVariantMap paramSpecs READ paramSpecs CONSTANT)
    Q_PROPERTY(QStringList effectOrder READ effectOrder NOTIFY paramsChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY presetsChanged)
    Q_PROPERTY(bool presetModified READ presetModified NOTIFY presetsChanged)
    Q_PROPERTY(bool rnnoiseAvailable READ rnnoiseAvailable CONSTANT)
    Q_PROPERTY(bool advancedAiAvailable READ advancedAiAvailable CONSTANT)
    // Voice character looks (Natural, Deep & Strong, ...) and the one the current values match ("" = custom).
    Q_PROPERTY(QVariantList voiceLooks READ voiceLooks CONSTANT)
    Q_PROPERTY(QString voiceLook READ voiceLook NOTIFY paramsChanged)

    // Live meters (~30 Hz)
    Q_PROPERTY(double inputLevelDb READ inputLevelDb NOTIFY metersChanged)
    Q_PROPERTY(double outputLevelDb READ outputLevelDb NOTIFY metersChanged)
    Q_PROPERTY(double voiceProbability READ voiceProbability NOTIFY metersChanged)
    Q_PROPERTY(double gateReductionDb READ gateReductionDb NOTIFY metersChanged)
    Q_PROPERTY(double compressorReductionDb READ compressorReductionDb NOTIFY metersChanged)
    Q_PROPERTY(QString activeSuppressor READ activeSuppressor NOTIFY metersChanged)

    // Engine status, statistics, hardware
    Q_PROPERTY(QString engineState READ engineState NOTIFY statusChanged)
    Q_PROPERTY(QString engineMessage READ engineMessage NOTIFY statusChanged)
    Q_PROPERTY(QString engineError READ engineError NOTIFY statusChanged)
    Q_PROPERTY(bool engineRunning READ engineRunning NOTIFY statusChanged)
    Q_PROPERTY(QString inputName READ inputName NOTIFY statusChanged)
    Q_PROPERTY(QString outputName READ outputName NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap streamInfo READ streamInfo NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap hardware READ hardware CONSTANT)
    Q_PROPERTY(QVariantMap cableTest READ cableTest NOTIFY cableTestChanged)
    Q_PROPERTY(QVariantMap voiceCheck READ voiceCheck NOTIFY voiceCheckChanged)
    Q_PROPERTY(QVariantMap isolation READ isolation NOTIFY isolationChanged)

    // Simple-mode support
    Q_PROPERTY(bool advancedUi READ advancedUi WRITE setAdvancedUi NOTIFY configChanged)

    // Background running
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY configChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY configChanged)
    Q_PROPERTY(bool startWithWindows READ startWithWindows WRITE setStartWithWindows NOTIFY configChanged)
    Q_PROPERTY(QVariantMap setup READ setup NOTIFY setupChanged)
    Q_PROPERTY(int noiseLevel READ noiseLevel NOTIFY paramsChanged)

public:
    explicit EngineController(QObject* parent = nullptr);
    ~EngineController() override;

    // Devices
    [[nodiscard]] QVariantList inputDevices() const { return inputDevices_; }
    [[nodiscard]] QVariantList outputDevices() const { return outputDevices_; }
    [[nodiscard]] QString inputDeviceId() const;
    [[nodiscard]] bool inputFollowDefault() const { return settings_.engine.inputFollowDefault; }
    void setInputFollowDefault(bool follow);
    [[nodiscard]] QString outputDeviceId() const;
    [[nodiscard]] QString monitorDeviceId() const;
    [[nodiscard]] bool monitorEnabled() const { return settings_.engine.monitorEnabled; }
    void setMonitorEnabled(bool enabled);
    [[nodiscard]] bool cableDetected() const { return cableDetected_; }
    [[nodiscard]] QString routingWarning() const { return routingWarning_; }

    // Config
    [[nodiscard]] bool engineEnabled() const { return settings_.engineEnabled; }
    void setEngineEnabled(bool enabled);
    [[nodiscard]] int bufferMode() const { return static_cast<int>(settings_.engine.bufferMode); }
    void setBufferMode(int mode);
    [[nodiscard]] double outputTargetMs() const { return settings_.engine.outputTargetMs; }
    void setOutputTargetMs(double ms);
    [[nodiscard]] bool autoRecover() const { return settings_.engine.autoRecover; }
    void setAutoRecover(bool enabled);
    [[nodiscard]] int logLevel() const { return static_cast<int>(settings_.logLevel); }
    void setLogLevel(int level);
    [[nodiscard]] QString logFilePath() const;
    [[nodiscard]] QString performanceMode() const { return QString::fromStdString(settings_.performanceMode); }
    [[nodiscard]] QVariantList performanceModes() const;
    [[nodiscard]] QString selectedBackend() const { return selectedBackend_; }
    void setSelectedBackend(const QString& id);

    // Params / presets
    [[nodiscard]] QVariantMap params() const;
    [[nodiscard]] QVariantMap paramSpecs() const;
    [[nodiscard]] QStringList effectOrder() const;
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QString activePreset() const { return QString::fromStdString(settings_.activePreset); }
    [[nodiscard]] bool presetModified() const { return presetModified_; }
    [[nodiscard]] bool rnnoiseAvailable() const;
    [[nodiscard]] bool advancedAiAvailable() const;
    [[nodiscard]] QVariantList voiceLooks() const;
    [[nodiscard]] QString voiceLook() const;

    // Meters
    [[nodiscard]] double inputLevelDb() const { return inputLevelDb_; }
    [[nodiscard]] double outputLevelDb() const { return outputLevelDb_; }
    [[nodiscard]] double voiceProbability() const { return voiceProbability_; }
    [[nodiscard]] double gateReductionDb() const { return gateReductionDb_; }
    [[nodiscard]] double compressorReductionDb() const { return compressorReductionDb_; }
    [[nodiscard]] QString activeSuppressor() const { return activeSuppressor_; }

    // Status
    [[nodiscard]] QString engineState() const;
    [[nodiscard]] QString engineMessage() const { return QString::fromStdString(status_.message); }
    [[nodiscard]] QString engineError() const { return QString::fromStdString(status_.lastError); }
    [[nodiscard]] bool engineRunning() const;
    [[nodiscard]] QString inputName() const { return QString::fromStdString(status_.info.inputName); }
    [[nodiscard]] QString outputName() const { return QString::fromStdString(status_.info.outputName); }
    [[nodiscard]] QVariantMap streamInfo() const;
    [[nodiscard]] QVariantMap stats() const { return stats_; }
    [[nodiscard]] QVariantMap hardware() const { return hardware_; }
    [[nodiscard]] QVariantMap cableTest() const { return cableTest_; }
    [[nodiscard]] QVariantMap voiceCheck() const { return voiceCheck_; }
    [[nodiscard]] QVariantMap isolation() const { return isolation_; }
    [[nodiscard]] bool advancedUi() const { return settings_.advancedUi; }
    void setAdvancedUi(bool enabled);
    [[nodiscard]] bool trayAvailable() const { return trayAvailable_; }
    void setTrayAvailable(bool available);
    [[nodiscard]] bool closeToTray() const { return settings_.closeToTray; }
    void setCloseToTray(bool enabled);
    // "Start with Windows": the per-user Run key, started with --minimized (straight to the tray).
    [[nodiscard]] bool startWithWindows() const;
    void setStartWithWindows(bool enabled);
    [[nodiscard]] QVariantMap setup() const { return setup_; }
    // 0 = Off, 1 = Light, 2 = Balanced, 3 = Strong, -1 = custom (fine-tuned in advanced controls)
    [[nodiscard]] int noiseLevel() const;

    Q_INVOKABLE void setParam(const QString& key, const QVariant& value);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void selectInputDevice(const QString& id);
    Q_INVOKABLE void selectOutputDevice(const QString& id);
    Q_INVOKABLE void selectMonitorDevice(const QString& id);
    Q_INVOKABLE void applyPreset(const QString& name);
    Q_INVOKABLE void applyVoiceLook(const QString& key);
    Q_INVOKABLE void saveCustomPreset(const QString& name);
    Q_INVOKABLE void deleteCustomPreset(const QString& name);
    Q_INVOKABLE void setPerformanceMode(const QString& key);
    Q_INVOKABLE void moveEffect(int index, int delta);
    Q_INVOKABLE void restartEngine();
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void runVirtualCableTest();
    // Records `seconds` of the processed voice, then plays it back on the monitor device
    // (or the Windows default output). In memory only; nothing is saved.
    Q_INVOKABLE void setNoiseLevel(int level);
    // One-click fixes offered by the setup checklist.
    Q_INVOKABLE void fixMicrophone();
    Q_INVOKABLE void fixOutput();
    Q_INVOKABLE void startVoiceCheck(double seconds = 5.0);
    Q_INVOKABLE void stopVoiceCheck();
    // Strict voice isolation: records the microphone for as long as the user likes (Done ends it, up to
    // 30 min), then enrolls it as "your voice" (saved locally under %LOCALAPPDATA%/VoiceStudio/voice,
    // with the full recording). Re-recording replaces it; delete removes the files.
    // append: add to the saved voice instead of replacing it ("Add more").
    Q_INVOKABLE void startVoiceEnrollment(bool append = false);
    Q_INVOKABLE void finishVoiceEnrollment();
    Q_INVOKABLE void cancelVoiceEnrollment();
    Q_INVOKABLE void deleteVoiceEnrollment();
    Q_INVOKABLE void openVoiceFolder();
    // Marks an audio device as a virtual microphone (e.g. the user's own driver) so routing treats it like a cable.
    Q_INVOKABLE void setDeviceVirtual(const QString& id, bool isVirtual);
    Q_INVOKABLE void resetStatistics();
    // Called by the window when it hides itself instead of closing (shows a one-time tray notice).
    Q_INVOKABLE void notifyHiddenToTray() { emit hiddenToTray(); }
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE QString diagnosticsReport() const;
    // The user guide (docs/user-guide.md, compiled into the app) as Markdown.
    Q_INVOKABLE QString userGuide() const;
    Q_INVOKABLE void copyToClipboard(const QString& text);

signals:
    void devicesChanged();
    void configChanged();
    void paramsChanged();
    void presetsChanged();
    void metersChanged();
    void statusChanged();
    void statsChanged();
    void cableTestChanged();
    void voiceCheckChanged();
    void isolationChanged();
    void hiddenToTray();
    void setupChanged();

private:
    void onDeviceEvent(const DeviceEvent& event);
    void onStatusFromSupervisor();
    void updateMeters();
    void updateStats();
    void pushConfig();
    void scheduleSave();
    void saveNow();
    void markCustomPerformance();
    void updateRoutingWarning();
    void autoSelectCableOutput();
    void pollVoiceCheck();
    void pollIsolation();
    void refreshIsolation();
    void refreshSetup();
    [[nodiscard]] const AudioDeviceInfo* findDevice(const std::vector<AudioDeviceInfo>& list, const std::wstring& id) const;
    [[nodiscard]] const AudioDeviceInfo* suggestedMicrophone() const;
    [[nodiscard]] const AudioDeviceInfo* suggestedOutput() const;
    // "VoiceStudio Mic Feed", the playback side of the VoiceStudio Virtual Microphone driver, if installed.
    [[nodiscard]] const AudioDeviceInfo* voiceStudioFeed() const;
    VirtualMicStatus virtualMic_{}; // the VoiceStudio Virtual Microphone driver, refreshed with the device list
    // Name of the microphone call apps should pick for the virtual microphone `output` plays into.
    [[nodiscard]] QString appMicrophoneName(const AudioDeviceInfo* output) const;
    [[nodiscard]] bool isUserVirtual(const std::wstring& id) const;
    void startVoicePlayback(std::vector<float> recording);
    void setVoiceCheck(const QString& state, const QString& message, double progress = 0.0);
    [[nodiscard]] QVariantMap buildHardwareMap() const;
    // Hands the usable processors (CPU + GPUs) and the saved choice to voice isolation.
    void applyProcessorChoice();
    [[nodiscard]] static QVariantList toVariantList(const std::vector<AudioDeviceInfo>& devices);

    QString settingsPath_;
    AppSettings settings_;
    EngineParameters params_;
    std::unique_ptr<DeviceManager> deviceManager_;
    std::unique_ptr<EngineSupervisor> supervisor_;
    StatsCollector statsCollector_;
    ProcessMetrics processMetrics_;
    HardwareInfo hardwareInfo_;

    std::vector<AudioDeviceInfo> inputDeviceInfos_;
    std::vector<AudioDeviceInfo> outputDeviceInfos_;
    QVariantList inputDevices_;
    QVariantList outputDevices_;
    bool cableDetected_ = false;
    QString routingWarning_;
    QString selectedBackend_ = QStringLiteral("auto");
    bool presetModified_ = false;

    EngineStatus status_{};
    QVariantMap stats_;
    QVariantMap hardware_;
    QVariantMap cableTest_;
    QVariantMap voiceCheck_;
    QVariantMap isolation_;
    bool trayAvailable_ = false;
    QString enrollPhase_ = QStringLiteral("idle"); // idle | recording | processing
    QString enrollMessage_;
    std::uint64_t enrollSubmitRevision_ = 0;
    bool enrollAppend_ = false; // the recording in progress is added to the saved voice
    std::uint64_t isolationRevisionSeen_ = ~0ull;
    std::vector<float> isolationInferenceUs_; // recent chunk.onnx times (ring)
    std::size_t isolationInferenceNext_ = 0;
    int isolationHealthTicks_ = 0;
    std::uint64_t isolationUnderrunsLogged_ = 0;
    QVariantMap setup_;
    bool micHeard_ = false; // a voice-level signal was seen on the current microphone
    std::thread playbackThread_;
    std::atomic<bool> playbackCancel_{false};

    double inputLevelDb_ = -120.0;
    double outputLevelDb_ = -120.0;
    double voiceProbability_ = -1.0;
    double gateReductionDb_ = 0.0;
    double compressorReductionDb_ = 0.0;
    QString activeSuppressor_;

    QTimer meterTimer_;
    QTimer statsTimer_;
    QTimer saveTimer_;
};

} // namespace vs
