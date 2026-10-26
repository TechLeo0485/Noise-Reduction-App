#include "app/EngineController.h"

#include "core/DspMath.h"
#include "diagnostics/LoopbackLatencyTester.h"
#include "diagnostics/Logger.h"
#include "diagnostics/VoicePlayback.h"
#include "settings/Presets.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>

namespace vs {

namespace {

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

QString qs(const std::wstring& s) {
    return QString::fromStdWString(s);
}

double round1(double v) {
    return std::round(v * 10.0) / 10.0;
}

double round2(double v) {
    return std::round(v * 100.0) / 100.0;
}

QVariantMap streamInfoMap(const StreamInfo& s, bool running) {
    QVariantMap m;
    m["running"] = running;
    m["format"] = running ? qs(s.format.describe()) : QString();
    m["sampleRate"] = static_cast<int>(s.format.sampleRate);
    m["channels"] = static_cast<int>(s.format.channels);
    m["periodMs"] = round2(s.periodMs);
    m["bufferMs"] = round2(s.bufferMs);
    m["streamLatencyMs"] = round2(s.streamLatencyMs);
    m["audioClient3"] = s.usedAudioClient3;
    return m;
}

bool containsCi(const std::wstring& haystack, const wchar_t* needle) {
    std::wstring h = haystack;
    std::wstring n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::towlower);
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return h.find(n) != std::wstring::npos;
}

const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kRunValue = QStringLiteral("VoiceStudio");

QString autostartCommand() {
    return QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

} // namespace

EngineController::EngineController(QObject* parent) : QObject(parent), settingsPath_(SettingsStore::defaultPath()) {
    Logger::instance().open(appDataDirectory() / L"logs" / L"voicestudio.log");

    QString loadError;
    if (!SettingsStore::load(settingsPath_, settings_, &loadError)) {
        logWarning("Settings reset to defaults: " + loadError.toStdString());
    }
    Logger::instance().setLevel(settings_.logLevel);
    settings_.applyParams(params_);
    logInfo("VoiceStudio starting");

    // GPUs can run voice isolation when the DirectML runtime ships next to the exe.
    const std::filesystem::path exeDir = QCoreApplication::applicationDirPath().toStdWString();
    std::error_code dmlError;
    hardwareInfo_ = detectHardware(std::filesystem::exists(exeDir / L"DirectML.dll", dmlError));
    selectedBackend_ = QString::fromStdString(settings_.processor);
    for (const GpuInfo& g : hardwareInfo_.gpus) {
        logInfo("Accelerator: " + g.name + " (" + g.id + ", DXGI " + std::to_string(g.dxgiIndex) + ")");
    }
    for (const std::string& ignored : hardwareInfo_.ignoredAdapters) {
        logInfo("Ignored adapter: " + ignored);
    }
    hardware_ = buildHardwareMap();

    deviceManager_ = std::make_unique<DeviceManager>();
    supervisor_ = std::make_unique<EngineSupervisor>(params_);
    supervisor_->setStatusListener([this] {
        QMetaObject::invokeMethod(this, [this] { onStatusFromSupervisor(); }, Qt::QueuedConnection);
    });
    deviceManager_->setEventCallback([this](const DeviceEvent& e) {
        QMetaObject::invokeMethod(this, [this, e] { onDeviceEvent(e); }, Qt::QueuedConnection);
    });

    refreshDevices();
    autoSelectCableOutput();
    updateRoutingWarning();

    cableTest_["running"] = false;
    cableTest_["hasResult"] = false;
    voiceCheck_["state"] = QStringLiteral("idle");
    voiceCheck_["progress"] = 0.0;
    voiceCheck_["message"] = QString();

    // Start with Windows: if the registered exe no longer exists (moved or reinstalled elsewhere), point the
    // entry at this one. A valid entry is left alone so a second copy (e.g. a dev build) cannot take it over.
    {
        QSettings run(kRunKey, QSettings::NativeFormat);
        const QString registered = run.value(kRunValue).toString();
        if (!registered.isEmpty()) {
            const QString exe = registered.section(QLatin1Char('"'), 1, 1);
            if (!exe.isEmpty() && !QFileInfo::exists(exe)) {
                run.setValue(kRunValue, autostartCommand());
                logInfo("Start with Windows now points to " + QCoreApplication::applicationFilePath().toStdString());
            }
        }
    }

    // Strict voice isolation: runtime and models ship next to the exe; the voice sample stays in local app data.
    const std::filesystem::path appDir = QCoreApplication::applicationDirPath().toStdWString();
    applyProcessorChoice(); // before initialize(): the first calibration already uses it
    supervisor_->engine().isolator().initialize(appDir, appDir / L"models" / L"voxlock",
                                                appDataDirectory() / L"voice" / L"voice_sample.wav");
    refreshIsolation();

    supervisor_->applyConfig(effectiveEngineConfig());
    supervisor_->launch();
    supervisor_->setEnabled(settings_.engineEnabled);

    meterTimer_.setInterval(33);
    connect(&meterTimer_, &QTimer::timeout, this, &EngineController::updateMeters);
    meterTimer_.start();
    statsTimer_.setInterval(500);
    connect(&statsTimer_, &QTimer::timeout, this, &EngineController::updateStats);
    statsTimer_.start();
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(1000);
    connect(&saveTimer_, &QTimer::timeout, this, &EngineController::saveNow);
}

EngineController::~EngineController() {
    meterTimer_.stop();
    statsTimer_.stop();
    playbackCancel_ = true;
    if (playbackThread_.joinable()) {
        playbackThread_.join();
    }
    if (deviceManager_) {
        deviceManager_->setEventCallback(nullptr);
    }
    if (supervisor_) {
        supervisor_->shutdown();
    }
    saveNow();
    logInfo("VoiceStudio stopped");
}

// ---- Devices ----------------------------------------------------------------------------

QVariantList EngineController::toVariantList(const std::vector<AudioDeviceInfo>& devices) {
    // Real devices first, virtual ones (cables, Voicemod, ...) after, so the obvious choice is on top.
    std::vector<const AudioDeviceInfo*> ordered;
    for (const AudioDeviceInfo& d : devices) {
        ordered.push_back(&d);
    }
    std::stable_partition(ordered.begin(), ordered.end(), [](const AudioDeviceInfo* d) { return !d->isVirtualCable; });

    QVariantList list;
    for (const AudioDeviceInfo* dp : ordered) {
        const AudioDeviceInfo& d = *dp;
        QVariantMap m;
        m["id"] = qs(d.id);
        m["name"] = qs(d.name);
        QString label = qs(d.name);
        if (d.isVirtualCable) {
            label += QStringLiteral("  -  virtual");
        }
        if (d.isDefault) {
            label += QStringLiteral("  -  Windows default");
        }
        m["label"] = label;
        m["isDefault"] = d.isDefault;
        m["isVirtualCable"] = d.isVirtualCable;
        m["format"] = d.formatSupported ? qs(d.mixFormat.describe()) : QStringLiteral("Unsupported format");
        m["formatSupported"] = d.formatSupported;
        list.append(m);
    }
    return list;
}

void EngineController::refreshDevices() {
    if (!deviceManager_ || !deviceManager_->isValid()) {
        return;
    }
    inputDeviceInfos_ = deviceManager_->enumerate(DeviceFlow::Capture);
    outputDeviceInfos_ = deviceManager_->enumerate(DeviceFlow::Render);
    virtualMic_ = queryVirtualMicDriver();
    // Devices the user marked as virtual (their own virtual microphone driver) count as cables everywhere.
    for (auto* list : {&inputDeviceInfos_, &outputDeviceInfos_}) {
        for (AudioDeviceInfo& d : *list) {
            d.isVirtualCable = d.isVirtualCable || isUserVirtual(d.id);
        }
    }

    // VB-CABLE installs a second, 16-channel input ("CABLE In 16ch") that feeds the same
    // "CABLE Output". It is redundant for voice and confusing, so hide it when the normal
    // "CABLE Input" exists, and move any saved selection over to "CABLE Input".
    const auto cableInput = std::find_if(outputDeviceInfos_.begin(), outputDeviceInfos_.end(), [](const AudioDeviceInfo& d) {
        return d.isVirtualCable && containsCi(d.name, L"cable input");
    });
    bool selectionMoved = false;
    if (cableInput != outputDeviceInfos_.end()) {
        const std::wstring cableInputId = cableInput->id;
        std::vector<std::wstring> hidden;
        for (const AudioDeviceInfo& d : outputDeviceInfos_) {
            if (containsCi(d.name, L"cable in 16")) {
                hidden.push_back(d.id);
            }
        }
        for (const std::wstring& id : hidden) {
            for (std::wstring* selected : {&settings_.engine.outputDeviceId, &settings_.engine.monitorDeviceId}) {
                if (*selected == id) {
                    *selected = cableInputId;
                    selectionMoved = true;
                }
            }
        }
        std::erase_if(outputDeviceInfos_, [&](const AudioDeviceInfo& d) {
            return std::find(hidden.begin(), hidden.end(), d.id) != hidden.end();
        });
    }
    inputDevices_ = toVariantList(inputDeviceInfos_);
    outputDevices_ = toVariantList(outputDeviceInfos_);
    for (QVariantList* list : {&inputDevices_, &outputDevices_}) {
        for (QVariant& v : *list) {
            QVariantMap m = v.toMap();
            m["userVirtual"] = isUserVirtual(m.value("id").toString().toStdWString());
            v = m;
        }
    }
    cableDetected_ = std::any_of(outputDeviceInfos_.begin(), outputDeviceInfos_.end(),
                                 [](const AudioDeviceInfo& d) { return d.isVirtualCable; });
    emit devicesChanged();
    if (selectionMoved && supervisor_) {
        logInfo("Switched from the 16-channel VB-CABLE input to \"CABLE Input\" (same cable)");
        pushConfig();
    }
    refreshSetup();
}

// ---- Simple mode: setup checklist, one-click fixes, noise levels ----------------------------

const AudioDeviceInfo* EngineController::findDevice(const std::vector<AudioDeviceInfo>& list, const std::wstring& id) const {
    for (const AudioDeviceInfo& d : list) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

const AudioDeviceInfo* EngineController::suggestedMicrophone() const {
    // Prefer the Windows default if it is a real microphone, otherwise the first real one.
    const AudioDeviceInfo* first = nullptr;
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (!d.isVirtualCable) {
            if (d.isDefault) {
                return &d;
            }
            if (first == nullptr) {
                first = &d;
            }
        }
    }
    return first;
}

EngineConfig EngineController::effectiveEngineConfig() const {
    // Following the Windows default microphone must never mean listening to a virtual microphone - typically
    // VoiceStudio's own, after it was made the Windows default for call apps. VoiceStudio would then process its
    // own output and nobody would hear the real voice. Use the real microphone instead (the setting is kept).
    EngineConfig config = settings_.engine;
    if (config.inputFollowDefault) {
        const bool defaultIsVirtual = std::any_of(inputDeviceInfos_.begin(), inputDeviceInfos_.end(),
                                                  [](const AudioDeviceInfo& d) { return d.isDefault && d.isVirtualCable; });
        if (defaultIsVirtual) {
            if (const AudioDeviceInfo* mic = suggestedMicrophone()) {
                config.inputFollowDefault = false;
                config.inputDeviceId = mic->id;
            }
        }
    }
    return config;
}

const AudioDeviceInfo* EngineController::voiceStudioFeed() const {
    // By endpoint name, or by the device it belongs to: Windows can keep an endpoint's old name after a driver
    // update (driver builds before 2026-10-25 showed it as "SinkDescription Sample (VoiceStudio Virtual Audio)").
    // The driver has only this one playback endpoint.
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (containsCi(d.name, L"voicestudio mic feed") || containsCi(d.name, L"(voicestudio virtual audio)")) {
            return &d;
        }
    }
    return nullptr;
}

const AudioDeviceInfo* EngineController::suggestedOutput() const {
    // The virtual microphone's playback side: a device the user marked first, then VoiceStudio's own
    // "VoiceStudio Mic Feed", then VB-CABLE's "CABLE Input", then any other virtual playback device.
    const AudioDeviceInfo* cable = nullptr;
    const AudioDeviceInfo* other = nullptr;
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (isUserVirtual(d.id)) {
            return &d;
        }
    }
    if (const AudioDeviceInfo* feed = voiceStudioFeed()) {
        return feed;
    }
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (d.isVirtualCable && cable == nullptr && containsCi(d.name, L"cable input")) {
            cable = &d;
        }
        if (d.isVirtualCable && other == nullptr) {
            other = &d;
        }
    }
    return cable != nullptr ? cable : other;
}

QString EngineController::appMicrophoneName(const AudioDeviceInfo* output) const {
    // The recording side call apps choose: for VB-CABLE "CABLE Input" feeds "CABLE Output"; for other drivers
    // the virtual microphone that shares the driver name in brackets, e.g. "Mic (My Virtual Audio)".
    if (output == nullptr) {
        return QStringLiteral("the virtual microphone");
    }
    const auto bracket = [](const std::wstring& name) {
        const std::size_t open = name.rfind(L'(');
        return open == std::wstring::npos ? std::wstring() : name.substr(open);
    };
    const std::wstring driver = bracket(output->name);
    const AudioDeviceInfo* any = nullptr;
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (!d.isVirtualCable) {
            continue;
        }
        if (containsCi(output->name, L"cable input") && containsCi(d.name, L"cable output")) {
            return qs(d.name);
        }
        if (!driver.empty() && bracket(d.name) == driver) {
            return qs(d.name);
        }
        if (any == nullptr) {
            any = &d;
        }
    }
    return any != nullptr ? qs(any->name) : QStringLiteral("the virtual microphone");
}

bool EngineController::isUserVirtual(const std::wstring& id) const {
    return std::find(settings_.virtualDeviceIds.begin(), settings_.virtualDeviceIds.end(), id) !=
           settings_.virtualDeviceIds.end();
}

void EngineController::setDeviceVirtual(const QString& id, bool isVirtual) {
    const std::wstring wid = id.toStdWString();
    if (wid.empty() || isUserVirtual(wid) == isVirtual) {
        return;
    }
    if (isVirtual) {
        settings_.virtualDeviceIds.push_back(wid);
    } else {
        std::erase(settings_.virtualDeviceIds, wid);
    }
    logInfo(std::string("Device marked as ") + (isVirtual ? "a virtual microphone" : "a normal device"));
    scheduleSave();
    refreshDevices();
    updateRoutingWarning();
}

void EngineController::refreshSetup() {
    const EngineRuntimeInfo& info = status_.info;
    const AudioDeviceInfo* mic = findDevice(inputDeviceInfos_, info.inputDeviceId);
    const AudioDeviceInfo* out = findDevice(outputDeviceInfos_, info.outputDeviceId);
    const AudioDeviceInfo* suggestedMic = suggestedMicrophone();
    const AudioDeviceInfo* suggestedOut = suggestedOutput();

    const bool micActive = info.captureRunning;
    const bool micIsVirtual = mic != nullptr && mic->isVirtualCable;
    const bool outputIsCable = info.outputRunning && out != nullptr && out->isVirtualCable;

    QVariantMap s;
    s["engineOn"] = settings_.engineEnabled;
    s["micActive"] = micActive;
    s["micName"] = micActive ? qs(info.inputName) : QString();
    s["micIsVirtual"] = micIsVirtual;
    s["micOk"] = micActive && !micIsVirtual;
    s["suggestedMicName"] = suggestedMic ? qs(suggestedMic->name) : QString();
    s["hasPhysicalMic"] = suggestedMic != nullptr;
    s["cableInstalled"] = cableDetected_;
    // VoiceStudio's own driver: installed but not running needs an explanation (Test Mode / Secure Boot).
    s["virtualMic"] = QString::fromLatin1(virtualMicStateName(virtualMic_.state));
    s["virtualMicProblem"] = static_cast<int>(virtualMic_.problemCode);
    s["testMode"] = virtualMic_.testMode;
    s["secureBoot"] = virtualMic_.secureBoot;
    s["outputName"] = info.outputRunning ? qs(info.outputName) : QString();
    s["outputOk"] = outputIsCable;
    s["appMicName"] = appMicrophoneName(outputIsCable ? out : suggestedOut);
    s["canFixOutput"] = suggestedOut != nullptr;
    s["heard"] = micHeard_;
    s["allOk"] = micActive && !micIsVirtual && outputIsCable && cableDetected_;
    if (s != setup_) {
        setup_ = s;
        emit setupChanged();
    }
}

void EngineController::fixMicrophone() {
    if (const AudioDeviceInfo* mic = suggestedMicrophone()) {
        settings_.engine.inputDeviceId = mic->id;
        settings_.engine.inputFollowDefault = false;
        micHeard_ = false;
        logInfo("Setup: microphone set to " + toUtf8(mic->name));
        pushConfig();
    }
}

void EngineController::fixOutput() {
    if (const AudioDeviceInfo* out = suggestedOutput()) {
        settings_.engine.outputDeviceId = out->id;
        settings_.engine.outputFollowDefault = false;
        logInfo("Setup: processed output set to " + toUtf8(out->name));
        pushConfig();
    }
}

namespace {
struct NoiseLevelPreset {
    bool suppression;
    float strength;
    bool gate;
    float gateRangeDb;
    float gateRatio;
};
// Balanced equals the engine defaults, so a fresh install shows "Balanced".
constexpr NoiseLevelPreset kNoiseLevels[] = {
    {false, 100.f, false, 30.f, 4.f}, // Off
    {true, 85.f, true, 15.f, 2.f},    // Light
    {true, 100.f, true, 30.f, 4.f},   // Balanced
    {true, 100.f, true, 50.f, 8.f},   // Strong
};
} // namespace

int EngineController::noiseLevel() const {
    const bool suppression = params_.getBool(ParamId::SuppressionEnabled);
    const bool gate = params_.getBool(ParamId::GateEnabled);
    if (!suppression && !gate) {
        return 0;
    }
    for (int i = 1; i < 4; ++i) {
        const NoiseLevelPreset& l = kNoiseLevels[i];
        if (suppression == l.suppression && gate == l.gate &&
            std::fabs(params_.get(ParamId::SuppressionStrength) - l.strength) < 0.5f &&
            std::fabs(params_.get(ParamId::GateRangeDb) - l.gateRangeDb) < 0.5f &&
            std::fabs(params_.get(ParamId::GateRatio) - l.gateRatio) < 0.05f) {
            return i;
        }
    }
    return -1;
}

void EngineController::setNoiseLevel(int level) {
    if (level < 0 || level > 3) {
        return;
    }
    const NoiseLevelPreset& l = kNoiseLevels[level];
    params_.set(ParamId::SuppressionEnabled, l.suppression ? 1.0f : 0.0f);
    params_.set(ParamId::SuppressionMode, rnnoiseAvailable() ? 1.0f : 0.0f);
    params_.set(ParamId::SuppressionStrength, l.strength);
    params_.set(ParamId::GateEnabled, l.gate ? 1.0f : 0.0f);
    params_.set(ParamId::GateRangeDb, l.gateRangeDb);
    params_.set(ParamId::GateRatio, l.gateRatio);
    if (!presetModified_) {
        presetModified_ = true;
        emit presetsChanged();
    }
    emit paramsChanged();
    scheduleSave();
}

void EngineController::setAdvancedUi(bool enabled) {
    if (settings_.advancedUi != enabled) {
        settings_.advancedUi = enabled;
        emit configChanged();
        scheduleSave();
    }
}

void EngineController::setTrayAvailable(bool available) {
    if (trayAvailable_ != available) {
        trayAvailable_ = available;
        emit configChanged();
    }
}

void EngineController::setCloseToTray(bool enabled) {
    if (settings_.closeToTray != enabled) {
        settings_.closeToTray = enabled;
        emit configChanged();
        scheduleSave();
    }
}

bool EngineController::startWithWindows() const {
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kRunValue);
}

void EngineController::setStartWithWindows(bool enabled) {
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled) {
        run.setValue(kRunValue, autostartCommand());
    } else {
        run.remove(kRunValue);
    }
    run.sync();
    logInfo(std::string("Start with Windows ") + (enabled ? "enabled" : "disabled"));
    emit configChanged();
}

void EngineController::autoSelectCableOutput() {
    // The first time the VoiceStudio Virtual Microphone is present, send the voice to it (also when VB-CABLE was
    // in use) - once only, so choosing another device afterwards sticks.
    if (const AudioDeviceInfo* feed = voiceStudioFeed(); feed != nullptr && !settings_.virtualMicAdopted) {
        settings_.virtualMicAdopted = true;
        if (settings_.engine.outputDeviceId != feed->id) {
            settings_.engine.outputDeviceId = feed->id;
            settings_.engine.outputFollowDefault = false;
            logInfo("VoiceStudio Virtual Microphone found: sending the voice to " + toUtf8(feed->name));
            pushConfig();
        }
        scheduleSave();
        return;
    }
    if (!settings_.engine.outputDeviceId.empty() || settings_.engine.outputFollowDefault) {
        return;
    }
    if (const AudioDeviceInfo* d = suggestedOutput()) {
        settings_.engine.outputDeviceId = d->id;
        logInfo("Auto-selected virtual microphone output: " + toUtf8(d->name));
        scheduleSave();
    }
}

void EngineController::onDeviceEvent(const DeviceEvent& event) {
    refreshDevices();
    autoSelectCableOutput(); // e.g. the VoiceStudio Virtual Microphone was just installed
    supervisor_->applyConfig(effectiveEngineConfig()); // the default microphone may have become a virtual one
    if (event.type == DeviceEventType::DefaultChanged) {
        supervisor_->notifyDefaultDeviceChanged(event.flow == DeviceFlow::Capture);
    } else {
        supervisor_->notifyDevicesChanged();
    }
    updateRoutingWarning();
}

QString EngineController::inputDeviceId() const {
    return qs(settings_.engine.inputDeviceId);
}

QString EngineController::outputDeviceId() const {
    return qs(settings_.engine.outputDeviceId);
}

QString EngineController::monitorDeviceId() const {
    return qs(settings_.engine.monitorDeviceId);
}

void EngineController::selectInputDevice(const QString& id) {
    settings_.engine.inputDeviceId = id.toStdWString();
    settings_.engine.inputFollowDefault = id.isEmpty();
    pushConfig();
}

void EngineController::setInputFollowDefault(bool follow) {
    if (!follow && settings_.engine.inputDeviceId.empty()) {
        // Turning "follow default" off pins the device currently in use, so the switch, the
        // device list and the engine all agree (previously the request was silently ignored).
        settings_.engine.inputDeviceId = !status_.info.inputDeviceId.empty()
                                             ? status_.info.inputDeviceId
                                             : deviceManager_->defaultDeviceId(DeviceFlow::Capture);
    }
    settings_.engine.inputFollowDefault = follow || settings_.engine.inputDeviceId.empty();
    pushConfig();
}

void EngineController::selectOutputDevice(const QString& id) {
    settings_.engine.outputDeviceId = id.toStdWString();
    settings_.engine.outputFollowDefault = false;
    pushConfig();
}

void EngineController::selectMonitorDevice(const QString& id) {
    settings_.engine.monitorDeviceId = id.toStdWString();
    pushConfig();
}

void EngineController::setMonitorEnabled(bool enabled) {
    settings_.engine.monitorEnabled = enabled;
    pushConfig();
}

void EngineController::updateRoutingWarning() {
    QString warning;
    const EngineConfig effective = effectiveEngineConfig(); // a virtual default microphone is already avoided there
    const std::wstring inputId =
        effective.inputFollowDefault ? deviceManager_->defaultDeviceId(DeviceFlow::Capture) : effective.inputDeviceId;
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (d.id == inputId && d.isVirtualCable) {
            warning = QStringLiteral("The microphone input is a virtual audio device (%1), not a physical microphone. "
                                     "That adds latency and can loop audio back on itself. Select your physical "
                                     "microphone in Setup.")
                          .arg(qs(d.name));
        }
    }
    if (warning.isEmpty() && !settings_.engine.outputDeviceId.empty()) {
        for (const AudioDeviceInfo& d : outputDeviceInfos_) {
            if (d.id == settings_.engine.outputDeviceId && !d.isVirtualCable) {
                warning = QStringLiteral("Your voice goes to \"%1\", a real speaker or headphone, so other apps will "
                                         "not receive it. Send it to your virtual microphone (e.g. \"CABLE Input\") and use "
                                         "\"Listen live\" to hear yourself. Using your own virtual mic? Mark it as virtual in Setup.")
                              .arg(qs(d.name));
            }
        }
    }
    if (warning.isEmpty() && settings_.engine.outputDeviceId.empty() && !settings_.engine.outputFollowDefault) {
        warning = cableDetected_ ? QStringLiteral("Send your voice to your virtual microphone (e.g. \"CABLE Input\") so other "
                                                  "apps can pick it as their microphone.")
                                 : QStringLiteral("No virtual microphone found. Install VB-CABLE, or mark your own virtual "
                                                  "microphone in Setup, to use your voice in Discord, Zoom or Teams.");
    }
    if (warning != routingWarning_) {
        routingWarning_ = warning;
        emit configChanged();
    }
}

// ---- Engine config ------------------------------------------------------------------------

void EngineController::pushConfig() {
    supervisor_->applyConfig(effectiveEngineConfig());
    updateRoutingWarning();
    emit configChanged();
    refreshSetup();
    scheduleSave();
}

void EngineController::setEngineEnabled(bool enabled) {
    settings_.engineEnabled = enabled;
    supervisor_->setEnabled(enabled);
    emit configChanged();
    refreshSetup();
    scheduleSave();
}

void EngineController::markCustomPerformance() {
    settings_.performanceMode = "custom";
}

void EngineController::setBufferMode(int mode) {
    settings_.engine.bufferMode = static_cast<BufferMode>(std::clamp(mode, 0, 2));
    markCustomPerformance();
    pushConfig();
}

void EngineController::setOutputTargetMs(double ms) {
    settings_.engine.outputTargetMs = std::clamp(ms, 2.0, 60.0); // part of the 300 ms delay budget
    markCustomPerformance();
    pushConfig();
}

void EngineController::setAutoRecover(bool enabled) {
    settings_.engine.autoRecover = enabled;
    pushConfig();
}

void EngineController::setLogLevel(int level) {
    settings_.logLevel = static_cast<LogLevel>(std::clamp(level, 0, 4));
    Logger::instance().setLevel(settings_.logLevel);
    emit configChanged();
    scheduleSave();
}

QString EngineController::logFilePath() const {
    return QString::fromStdWString(Logger::instance().path().wstring());
}

QVariantList EngineController::performanceModes() const {
    QVariantList list;
    for (const PerformanceProfile& p : performanceProfiles()) {
        QVariantMap m;
        m["key"] = QString::fromLatin1(p.key);
        m["label"] = QString::fromLatin1(p.label);
        m["description"] = QString::fromLatin1(p.description);
        m["targetMs"] = p.outputTargetMs;
        list.append(m);
    }
    return list;
}

void EngineController::setPerformanceMode(const QString& key) {
    const PerformanceProfile* profile = findPerformanceProfile(key.toStdString());
    if (profile == nullptr) {
        return;
    }
    settings_.performanceMode = profile->key;
    if (profile->mode != PerformanceMode::Custom) {
        settings_.engine.bufferMode = profile->bufferMode;
        settings_.engine.outputTargetMs = profile->outputTargetMs;
        for (const auto& [id, value] : profile->values) {
            params_.set(id, value);
        }
        emit paramsChanged();
    }
    pushConfig();
}

void EngineController::setSelectedBackend(const QString& id) {
    // Only backends that can actually run are selectable; others are shown disabled.
    bool known = id == QLatin1String("auto");
    for (const ProcessingBackend& b : hardwareInfo_.backends) {
        if (qs(b.id) == id) {
            if (!b.available) {
                return;
            }
            known = true;
        }
    }
    if (!known || id == selectedBackend_) {
        return;
    }
    selectedBackend_ = id;
    settings_.processor = id.toStdString();
    logInfo("Processor for voice isolation: " + settings_.processor);
    applyProcessorChoice();
    emit configChanged();
    scheduleSave();
}

void EngineController::applyProcessorChoice() {
    // CPU first, then the GPUs that can run the model (fastest first), each with its DirectML device.
    std::vector<IsolationProcessor> processors{{"cpu", "CPU", -1}};
    for (const GpuInfo& g : hardwareInfo_.gpus) {
        for (const ProcessingBackend& b : hardwareInfo_.backends) {
            if (b.id == g.id && b.available) {
                processors.push_back({g.id, g.name, g.dxgiIndex});
            }
        }
    }
    // A saved choice whose GPU is gone (or unusable now) falls back to automatic.
    std::string preferred = settings_.processor;
    if (preferred != "auto" && std::none_of(processors.begin(), processors.end(),
                                            [&](const IsolationProcessor& p) { return p.id == preferred; })) {
        logWarning("Saved processor " + preferred + " is not available on this PC; using automatic choice");
        preferred = "auto";
    }
    supervisor_->engine().isolator().setProcessors(std::move(processors), preferred);
}

void EngineController::restartEngine() {
    supervisor_->requestRestart();
}

void EngineController::resetToDefaults() {
    const auto customPresets = settings_.customPresets;
    const auto virtualDeviceIds = settings_.virtualDeviceIds;
    settings_ = AppSettings{};
    settings_.customPresets = customPresets; // user-created presets are kept
    settings_.virtualDeviceIds = virtualDeviceIds; // and so are the devices marked as virtual microphones
    params_.resetToDefaults();
    if (const Preset* p = findBuiltInPreset(settings_.activePreset)) {
        vs::applyPreset(*p, params_);
    }
    refreshDevices();
    autoSelectCableOutput();
    Logger::instance().setLevel(settings_.logLevel);
    presetModified_ = false;
    supervisor_->setEnabled(settings_.engineEnabled);
    emit paramsChanged();
    emit presetsChanged();
    pushConfig();
    refreshSetup();
}

// ---- Parameters & presets ----------------------------------------------------------------

QVariantMap EngineController::params() const {
    QVariantMap m;
    for (const ParamSpec& spec : vs::paramSpecs()) {
        const float v = params_.get(spec.id);
        if (spec.kind == ParamKind::Bool) {
            m[QLatin1String(spec.key)] = v >= 0.5f;
        } else if (spec.kind == ParamKind::Choice) {
            m[QLatin1String(spec.key)] = static_cast<int>(v);
        } else {
            m[QLatin1String(spec.key)] = static_cast<double>(v);
        }
    }
    return m;
}

QVariantMap EngineController::paramSpecs() const {
    QVariantMap m;
    for (const ParamSpec& spec : vs::paramSpecs()) {
        QVariantMap s;
        s["label"] = QString::fromLatin1(spec.label);
        s["min"] = static_cast<double>(spec.minValue);
        s["max"] = static_cast<double>(spec.maxValue);
        s["default"] = static_cast<double>(spec.defaultValue);
        s["step"] = static_cast<double>(spec.step);
        s["unit"] = QString::fromLatin1(spec.unit);
        m[QLatin1String(spec.key)] = s;
    }
    return m;
}

void EngineController::setParam(const QString& key, const QVariant& value) {
    const ParamSpec* spec = findParam(key.toStdString());
    if (spec == nullptr) {
        logWarning("Unknown parameter from UI: " + key.toStdString());
        return;
    }
    const float requested = value.typeId() == QMetaType::Bool ? (value.toBool() ? 1.0f : 0.0f) : value.toFloat();
    if (params_.get(spec->id) == clampParam(spec->id, requested)) {
        return;
    }
    params_.set(spec->id, requested);
    if (spec->id == ParamId::IsolationEnabled) {
        logInfo(std::string("Strict voice isolation ") + (params_.getBool(spec->id) ? "enabled" : "disabled"));
        refreshIsolation();
    }
    if (isPresetScoped(spec->id) && !presetModified_) {
        presetModified_ = true;
        emit presetsChanged();
    }
    emit paramsChanged();
    scheduleSave();
}

QVariantList EngineController::voiceLooks() const {
    QVariantList list;
    for (const VoiceLook& look : vs::voiceLooks()) {
        QVariantMap m;
        m["key"] = QString::fromLatin1(look.key);
        m["label"] = QString::fromUtf8(look.label);
        m["description"] = QString::fromUtf8(look.description);
        list.append(m);
    }
    return list;
}

QString EngineController::voiceLook() const {
    const VoiceLook* look = matchVoiceLook(params_);
    return look != nullptr ? QString::fromLatin1(look->key) : QString();
}

void EngineController::applyVoiceLook(const QString& key) {
    for (const VoiceLook& look : vs::voiceLooks()) {
        if (key == QLatin1String(look.key)) {
            vs::applyVoiceLook(look, params_);
            logInfo(std::string("Voice character: ") + look.label);
            emit paramsChanged();
            scheduleSave();
            return;
        }
    }
}

QStringList EngineController::effectOrder() const {
    QStringList list;
    for (EffectSlot slot : params_.effectOrder()) {
        list.append(QString::fromLatin1(effectSlotKey(slot)));
    }
    return list;
}

void EngineController::moveEffect(int index, int delta) {
    EffectOrder order = params_.effectOrder();
    const int target = index + delta;
    if (index < 0 || target < 0 || index >= static_cast<int>(order.size()) || target >= static_cast<int>(order.size())) {
        return;
    }
    std::swap(order[static_cast<std::size_t>(index)], order[static_cast<std::size_t>(target)]);
    params_.setEffectOrder(order);
    emit paramsChanged();
    scheduleSave();
}

QVariantList EngineController::presets() const {
    QVariantList list;
    for (const Preset& p : builtInPresets()) {
        QVariantMap m;
        m["name"] = qs(p.name);
        m["description"] = qs(p.description);
        m["builtIn"] = true;
        list.append(m);
    }
    for (const auto& [name, values] : settings_.customPresets) {
        QVariantMap m;
        m["name"] = qs(name);
        m["description"] = QStringLiteral("Custom preset");
        m["builtIn"] = false;
        list.append(m);
    }
    return list;
}

void EngineController::applyPreset(const QString& name) {
    const std::string key = name.toStdString();
    if (const Preset* p = findBuiltInPreset(key)) {
        vs::applyPreset(*p, params_);
    } else if (auto it = settings_.customPresets.find(key); it != settings_.customPresets.end()) {
        applyPresetValues(it->second, params_);
    } else {
        return;
    }
    settings_.activePreset = key;
    presetModified_ = false;
    emit paramsChanged();
    emit presetsChanged();
    scheduleSave();
}

void EngineController::saveCustomPreset(const QString& name) {
    const QString trimmed = name.trimmed().left(64);
    if (trimmed.isEmpty() || findBuiltInPreset(trimmed.toStdString()) != nullptr) {
        return; // built-in names are reserved
    }
    settings_.customPresets[trimmed.toStdString()] = capturePresetValues(params_);
    settings_.activePreset = trimmed.toStdString();
    presetModified_ = false;
    emit presetsChanged();
    scheduleSave();
}

void EngineController::deleteCustomPreset(const QString& name) {
    if (settings_.customPresets.erase(name.toStdString()) > 0) {
        emit presetsChanged();
        scheduleSave();
    }
}

bool EngineController::rnnoiseAvailable() const {
    return supervisor_->engine().isSuppressorAvailable(SuppressorKind::RNNoise);
}

bool EngineController::advancedAiAvailable() const {
    return supervisor_->engine().isSuppressorAvailable(SuppressorKind::AdvancedAI);
}

// ---- Status, meters, statistics -----------------------------------------------------------

void EngineController::onStatusFromSupervisor() {
    const std::wstring previousInput = status_.info.inputDeviceId;
    status_ = supervisor_->status();
    if (status_.info.inputDeviceId != previousInput) {
        micHeard_ = false; // a different microphone has to prove itself again
    }
    refreshSetup();
    if (status_.state == EngineState::Running || status_.state == EngineState::Degraded) {
        statsCollector_.clear();
    }
    emit statusChanged();
}

QString EngineController::engineState() const {
    return QString::fromLatin1(engineStateName(status_.state));
}

bool EngineController::engineRunning() const {
    return status_.state == EngineState::Running || status_.state == EngineState::Degraded;
}

QVariantMap EngineController::streamInfo() const {
    QVariantMap m;
    m["capture"] = streamInfoMap(status_.info.capture, status_.info.captureRunning);
    m["output"] = streamInfoMap(status_.info.output, status_.info.outputRunning);
    m["monitor"] = streamInfoMap(status_.info.monitor, status_.info.monitorRunning);
    m["inputName"] = qs(status_.info.inputName);
    m["outputName"] = qs(status_.info.outputName);
    m["monitorName"] = qs(status_.info.monitorName);
    m["outputError"] = qs(status_.info.outputError);
    m["monitorError"] = qs(status_.info.monitorError);
    m["captureResampling"] = status_.info.captureResampling;
    m["outputTargetMs"] = round1(status_.info.outputTargetMs);
    return m;
}

void EngineController::updateMeters() {
    const EngineMetrics& m = supervisor_->engine().metrics();
    const bool running = engineRunning();
    inputLevelDb_ = running ? linearToDb(m.inputPeak.load(std::memory_order_relaxed)) : -120.0;
    outputLevelDb_ = running ? linearToDb(m.outputPeak.load(std::memory_order_relaxed)) : -120.0;
    voiceProbability_ = m.voiceProbability.load(std::memory_order_relaxed);
    gateReductionDb_ = m.gateGainDb.load(std::memory_order_relaxed);
    compressorReductionDb_ = m.compressorGainDb.load(std::memory_order_relaxed);
    stabilizerGainDb_ = running ? m.stabilizerGainDb.load(std::memory_order_relaxed) : 0.0;
    stabilizerToneDb_ = running ? m.stabilizerToneDb.load(std::memory_order_relaxed) : 0.0;
    activeSuppressor_ = QString::fromLatin1(
        suppressorKindName(static_cast<SuppressorKind>(m.activeSuppressor.load(std::memory_order_relaxed))));
    emit metersChanged();
    pollVoiceCheck();
    pollIsolation();

    // "We can hear you": a speech-level peak on the current microphone (about -45 dBFS or louder).
    if (!micHeard_ && running && inputLevelDb_ > -45.0) {
        micHeard_ = true;
        refreshSetup();
    }
}

// ---- Voice check ------------------------------------------------------------------------------

void EngineController::setVoiceCheck(const QString& state, const QString& message, double progress) {
    voiceCheck_["state"] = state;
    voiceCheck_["message"] = message;
    voiceCheck_["progress"] = progress;
    emit voiceCheckChanged();
}

void EngineController::startVoiceCheck(double seconds) {
    const QString state = voiceCheck_.value("state").toString();
    if (state == QLatin1String("recording") || state == QLatin1String("playing")) {
        return;
    }
    if (!engineRunning()) {
        setVoiceCheck(QStringLiteral("error"), QStringLiteral("Start the audio engine first."));
        return;
    }
    if (supervisor_->engine().startVoiceCheck(std::clamp(seconds, 1.0, 10.0))) {
        setVoiceCheck(QStringLiteral("recording"), QStringLiteral("Recording - speak normally..."));
    }
}

void EngineController::stopVoiceCheck() {
    supervisor_->engine().cancelVoiceCheck();
    playbackCancel_ = true;
    setVoiceCheck(QStringLiteral("idle"), QString());
}

void EngineController::pollVoiceCheck() {
    if (voiceCheck_.value("state").toString() != QLatin1String("recording")) {
        return;
    }
    AudioEngine& engine = supervisor_->engine();
    if (!engineRunning()) {
        engine.cancelVoiceCheck();
        setVoiceCheck(QStringLiteral("error"), QStringLiteral("The audio engine stopped during recording."));
        return;
    }
    if (engine.voiceCheckState() == AudioEngine::VoiceCheckState::Ready) {
        startVoicePlayback(engine.takeVoiceCheck());
        return;
    }
    voiceCheck_["progress"] = static_cast<double>(engine.voiceCheckProgress());
    emit voiceCheckChanged();
}

void EngineController::startVoicePlayback(std::vector<float> recording) {
    if (playbackThread_.joinable()) {
        playbackThread_.join(); // previous playback already finished or was cancelled
    }
    if (recording.empty()) {
        setVoiceCheck(QStringLiteral("error"), QStringLiteral("Nothing was recorded."));
        return;
    }
    // Play on the monitoring headphones if one is chosen, otherwise the Windows default output.
    const std::wstring device = settings_.engine.monitorDeviceId;
    setVoiceCheck(QStringLiteral("playing"), QStringLiteral("Playing back what others hear..."), 1.0);
    playbackCancel_ = false;
    playbackThread_ = std::thread([this, device, clip = std::move(recording)] {
        ComInitializer com(COINIT_MULTITHREADED);
        std::string deviceName;
        std::string error;
        const bool ok = playMonoBuffer(device, clip, playbackCancel_, deviceName, error);
        const bool cancelled = playbackCancel_.load();
        QMetaObject::invokeMethod(
            this,
            [this, ok, cancelled, deviceName, error] {
                if (cancelled) {
                    return; // stopVoiceCheck() already reset the state
                }
                if (ok) {
                    setVoiceCheck(QStringLiteral("done"),
                                  QStringLiteral("Played on %1. That is exactly what others hear.").arg(qs(deviceName)));
                } else {
                    setVoiceCheck(QStringLiteral("error"), qs(error));
                }
            },
            Qt::QueuedConnection);
    });
}

void EngineController::updateStats() {
    EngineMetrics& m = supervisor_->engine().metrics();
    statsCollector_.drain(m);
    const TimingStats frame = statsCollector_.frameStats();
    const TimingStats callback = statsCollector_.callbackStats();
    const ProcessSample proc = processMetrics_.sample();
    const EngineRuntimeInfo& info = status_.info;
    const bool running = engineRunning();

    QVariantMap s;
    s["running"] = running;
    s["frameSamples"] = static_cast<qulonglong>(frame.samples);
    s["frameAvgUs"] = round1(frame.average);
    s["frameP50Us"] = round1(frame.p50);
    s["frameP95Us"] = round1(frame.p95);
    s["frameP99Us"] = round1(frame.p99);
    s["frameMaxUs"] = round1(frame.max);
    s["callbackAvgUs"] = round1(callback.average);
    s["callbackP95Us"] = round1(callback.p95);
    s["callbackP99Us"] = round1(callback.p99);
    s["callbackMaxUs"] = round1(callback.max);
    s["cpuLoadOfFrameBudget"] = round1(frame.average / 10000.0 * 100.0); // % of the 10 ms real-time budget

    s["framesProcessed"] = static_cast<qulonglong>(m.framesProcessed.load());
    s["deadlineMisses"] = static_cast<qulonglong>(m.deadlineMisses.load());
    s["captureGlitches"] = static_cast<qulonglong>(m.captureDiscontinuities.load());
    s["outputOverruns"] = static_cast<qulonglong>(m.outputOverruns.load());
    s["outputUnderruns"] = static_cast<qulonglong>(m.output.underruns.load());
    s["latencyResets"] = static_cast<qulonglong>(m.output.latencyResets.load());
    s["monitorUnderruns"] = static_cast<qulonglong>(m.monitor.underruns.load());
    s["nonFiniteRepairs"] = static_cast<qulonglong>(m.nonFiniteRepairs.load());
    s["driftPpm"] = round1(m.output.driftCorrectionPpm.load());
    s["requestedModeUnavailable"] = m.requestedModeUnavailable.load();

    // Latency breakdown. Components are measured where possible and labelled otherwise.
    const double algorithmicMs = m.algorithmicLatencySamples.load() * 1000.0 / VoiceProcessingChain::kSampleRate;
    const double capturePeriod = info.captureRunning ? info.capture.periodMs : 0.0;
    const double framingMs = info.captureRunning ? std::max(0.0, 10.0 - capturePeriod) : 0.0;
    const double resamplerMs = info.captureResamplerMs + (info.outputRunning ? info.outputResamplerMs : 0.0);
    const double jitterMs = info.outputRunning ? m.output.bufferedMs.load() : 0.0;
    const double renderQueueMs = info.outputRunning ? m.output.devicePaddingMs.load() : 0.0;
    const double processingMs = frame.p50 / 1000.0;
    s["latencyCapturePeriodMs"] = round2(capturePeriod);
    s["latencyFramingMs"] = round2(framingMs);
    s["latencyAlgorithmicMs"] = round2(algorithmicMs);
    s["latencyProcessingMs"] = round2(processingMs);
    s["latencyResamplerMs"] = round2(resamplerMs);
    s["latencyJitterBufferMs"] = round2(jitterMs);
    s["latencyRenderQueueMs"] = round2(renderQueueMs);
    s["latencyEstimatedTotalMs"] =
        running && info.outputRunning ? round1(capturePeriod + framingMs + algorithmicMs + processingMs + resamplerMs + jitterMs + renderQueueMs)
                                      : -1.0;
    s["processingAddedMs"] = round2(framingMs + algorithmicMs + processingMs + resamplerMs + jitterMs);
    // The 300 ms limit (microphone to the app recording the virtual microphone) and how it is shared out.
    s["latencyBudgetMs"] = kMaxTotalLatencyMs;
    s["latencyWithVirtualMicMs"] =
        running && info.outputRunning ? round1(capturePeriod + framingMs + algorithmicMs + processingMs + resamplerMs +
                                               jitterMs + renderQueueMs + kVirtualMicLatencyMs)
                                      : -1.0;
    s["isolationMaxMs"] = round1(info.isolationMaxMs);

    // Strict voice isolation: worker-thread inference times and fail-closed counters.
    VoiceIsolator::Metrics& iso = supervisor_->engine().isolator().metrics();
    if (isolationInferenceUs_.empty()) {
        isolationInferenceUs_.assign(2048, -1.0f); // ~30 s of history at 16 ms chunks
    }
    float inferenceUs = 0.0f;
    while (iso.inferenceUs.pop(&inferenceUs, 1) == 1) {
        isolationInferenceUs_[isolationInferenceNext_] = inferenceUs;
        isolationInferenceNext_ = (isolationInferenceNext_ + 1) % isolationInferenceUs_.size();
    }
    std::vector<float> recent;
    for (float v : isolationInferenceUs_) {
        if (v >= 0.0f) {
            recent.push_back(v);
        }
    }
    std::sort(recent.begin(), recent.end());
    auto percentileMs = [&recent](double q) {
        if (recent.empty()) {
            return -1.0;
        }
        return recent[std::min(recent.size() - 1, static_cast<std::size_t>(q * static_cast<double>(recent.size())))] / 1000.0;
    };
    const bool isolationOn = params_.getBool(ParamId::IsolationEnabled);
    s["isolationEnabled"] = isolationOn;
    s["isolationDelivering"] = iso.delivering.load();
    s["isolationLatencyMs"] = isolationOn ? round1(iso.latencyMs.load()) : 0.0;
    s["isolationInferenceP50Ms"] = round2(percentileMs(0.50));
    s["isolationInferenceP99Ms"] = round2(percentileMs(0.99));
    s["isolationInferenceMaxMs"] = round2(recent.empty() ? -1.0 : recent.back() / 1000.0);
    s["isolationUnderruns"] = static_cast<qulonglong>(iso.underruns.load());
    s["isolationOverruns"] = static_cast<qulonglong>(iso.overruns.load());
    s["isolationModelFailures"] = static_cast<qulonglong>(iso.modelFailures.load());
    s["isolationLatencyCaps"] = static_cast<qulonglong>(iso.latencyCaps.load());
    s["isolationSilencedFrames"] = static_cast<qulonglong>(iso.silencedFrames.load());
    s["isolationIsolatedFrames"] = static_cast<qulonglong>(iso.isolatedFrames.load());

    // One health line per minute while isolation is on, so problems on other PCs can be diagnosed from the log.
    if (isolationOn && running) {
        if (++isolationHealthTicks_ >= 120) { // stats run every 500 ms
            isolationHealthTicks_ = 0;
            const IsolationStatus st = supervisor_->engine().isolator().status();
            const std::uint64_t underruns = iso.underruns.load();
            logInfo("Isolation health: state " + std::string(isolationStateName(st.state)) + ", " +
                    std::to_string(st.framesPerRun * 8) + " ms chunks, latency " +
                    std::to_string(static_cast<int>(iso.latencyMs.load())) + " ms, inference p50 " +
                    std::to_string(percentileMs(0.50)) + " ms / p99 " + std::to_string(percentileMs(0.99)) +
                    " ms, late chunks +" + std::to_string(underruns - isolationUnderrunsLogged_) + " (total " +
                    std::to_string(underruns) + "), inference errors " + std::to_string(iso.modelFailures.load()));
            isolationUnderrunsLogged_ = underruns;
        }
    } else {
        isolationHealthTicks_ = 0;
    }

    s["processCpuPercent"] = proc.cpuPercent >= 0.0 ? round1(proc.cpuPercent) : -1.0;
    s["workingSetMb"] = round1(proc.workingSetMb);
    s["privateMb"] = round1(proc.privateBytesMb);
    s["restartCount"] = static_cast<qulonglong>(status_.restartCount);
    stats_ = s;
    emit statsChanged();
    refreshIsolation(); // delivering / silencing flags
}

void EngineController::resetStatistics() {
    statsCollector_.clear();
}

// ---- Strict voice isolation ---------------------------------------------------------------------

namespace {
constexpr double kEnrollMinSeconds = 5.0; // Done is offered from here; the model itself needs 3 s
}

void EngineController::startVoiceEnrollment(bool append) {
    if (enrollPhase_ != QLatin1String("idle")) {
        return;
    }
    enrollAppend_ = append && supervisor_->engine().isolator().status().enrolled;
    VoiceIsolator& isolator = supervisor_->engine().isolator();
    const IsolationState state = isolator.state();
    if (state != IsolationState::NeedsEnrollment && state != IsolationState::Ready) {
        enrollMessage_ = QStringLiteral("Voice isolation is not ready yet.");
    } else if (!engineRunning()) {
        enrollMessage_ = QStringLiteral("Start the audio engine first.");
    } else if (isolator.startRecording()) {
        enrollPhase_ = QStringLiteral("recording");
        enrollMessage_ = QStringLiteral("Recording - talk as long as you like, then press Done.");
        logInfo(enrollAppend_ ? "Voice enrollment recording started (adding to the saved voice)"
                              : "Voice enrollment recording started");
    }
    refreshIsolation();
}

void EngineController::finishVoiceEnrollment() {
    VoiceIsolator& isolator = supervisor_->engine().isolator();
    if (enrollPhase_ != QLatin1String("recording") || isolator.recordingSeconds() < kEnrollMinSeconds) {
        return;
    }
    isolator.stopRecording();
    pollIsolation(); // picks up the Ready state and enrolls
}

void EngineController::openVoiceFolder() {
    const std::filesystem::path folder = supervisor_->engine().isolator().voiceFolder();
    if (!folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(folder.wstring())));
    }
}

void EngineController::cancelVoiceEnrollment() {
    supervisor_->engine().isolator().cancelRecording();
    if (enrollPhase_ == QLatin1String("recording")) {
        enrollPhase_ = QStringLiteral("idle");
        enrollMessage_ = QStringLiteral("Recording cancelled. Your previous voice sample (if any) is unchanged.");
    }
    refreshIsolation();
}

void EngineController::deleteVoiceEnrollment() {
    cancelVoiceEnrollment();
    enrollMessage_.clear();
    supervisor_->engine().isolator().deleteEnrollment();
    refreshIsolation();
}

void EngineController::pollIsolation() {
    VoiceIsolator& isolator = supervisor_->engine().isolator();
    bool changed = false;
    if (enrollPhase_ == QLatin1String("recording")) {
        isolator.pumpRecording();
        if (!engineRunning()) {
            isolator.cancelRecording();
            enrollPhase_ = QStringLiteral("idle");
            enrollMessage_ = QStringLiteral("The audio engine stopped during recording. Please try again.");
        } else if (isolator.recordingState() == VoiceIsolator::RecordingState::Ready) {
            if (const std::size_t dropped = isolator.recordingDropped(); dropped > 0) {
                logWarning("Voice recording: " + std::to_string(dropped) + " samples were lost (UI was busy)");
            }
            const double seconds = isolator.recordingSeconds();
            enrollSubmitRevision_ = isolator.statusRevision();
            isolator.enroll(isolator.takeRecording16k(), status_.info.inputName, enrollAppend_);
            logInfo("Voice recording finished: " + std::to_string(static_cast<int>(seconds)) + " s");
            enrollPhase_ = QStringLiteral("processing");
            enrollMessage_.clear();
        }
        changed = true; // progress and level meter
    } else if (enrollPhase_ == QLatin1String("processing")) {
        if (isolator.statusRevision() != enrollSubmitRevision_ && !isolator.status().busy) {
            enrollPhase_ = QStringLiteral("idle");
            changed = true;
        }
    }
    if (changed || isolator.statusRevision() != isolationRevisionSeen_) {
        refreshIsolation();
    }
}

void EngineController::refreshIsolation() {
    VoiceIsolator& isolator = supervisor_->engine().isolator();
    isolationRevisionSeen_ = isolator.statusRevision();
    const IsolationStatus st = isolator.status();
    const VoiceIsolator::Metrics& m = isolator.metrics();
    const bool enabled = params_.getBool(ParamId::IsolationEnabled);
    const bool delivering = m.delivering.load(std::memory_order_relaxed);

    QVariantMap iso;
    iso["state"] = QString::fromLatin1(isolationStateName(st.state));
    iso["available"] = st.state != IsolationState::NotBuilt && st.state != IsolationState::Error;
    iso["message"] = enrollMessage_.isEmpty() ? qs(st.message) : enrollMessage_;
    iso["busy"] = st.busy || enrollPhase_ == QLatin1String("processing");
    iso["enrolled"] = st.enrolled;
    iso["ready"] = st.state == IsolationState::Ready;
    iso["phase"] = enrollPhase_;
    iso["recordSeconds"] = round1(isolator.recordingSeconds());
    iso["recordMinSeconds"] = kEnrollMinSeconds;
    iso["savedSeconds"] = round1(st.savedSeconds);
    iso["appending"] = enrollAppend_ && enrollPhase_ != QLatin1String("idle");
    iso["prepareProgress"] = st.prepareProgress;
    iso["recordMaxSeconds"] = VoiceIsolator::kRecordMaxSeconds;
    iso["recordProgress"] = static_cast<double>(isolator.recordingProgress());
    iso["recordLevelDb"] = round1(linearToDb(isolator.recordingPeak()));
    iso["enabled"] = enabled;
    iso["delivering"] = delivering;
    // Enabled but not delivering: the microphone is being silenced (no voice yet, loading, error, ...).
    iso["silencing"] = enabled && engineRunning() && !delivering;
    iso["backend"] = qs(st.backend);
    iso["framesPerRun"] = st.framesPerRun;
    iso["chunkMs"] = st.framesPerRun * 8;
    iso["threads"] = st.threads;
    iso["calibrationP50Ms"] = round2(st.calibrationP50Ms);
    iso["calibrationP95Ms"] = round2(st.calibrationP95Ms);
    iso["processorId"] = qs(st.processorId);
    iso["processorName"] = qs(st.processorName);
    QVariantList trials;
    for (const ProcessorTrial& t : st.trials) {
        QVariantMap trial;
        trial["id"] = qs(t.id);
        trial["name"] = qs(t.name);
        trial["chunkMs"] = t.framesPerRun * 8;
        trial["p50Ms"] = round2(t.p50Ms);
        trial["p95Ms"] = round2(t.p95Ms);
        trial["realTime"] = t.realTime;
        trial["error"] = qs(t.error);
        trials.append(trial);
    }
    iso["trials"] = trials;
    iso["adaptations"] = st.adaptations;
    // The voice sample only matches the microphone it was recorded with.
    iso["enrolledMicrophone"] = qs(st.enrolledMicrophone);
    iso["microphoneMismatch"] = st.enrolled && !st.enrolledMicrophone.empty() && !status_.info.inputName.empty() &&
                                st.enrolledMicrophone != status_.info.inputName;
    isolation_ = iso;
    emit isolationChanged();
}

// ---- Diagnostics ------------------------------------------------------------------------------

void EngineController::runVirtualCableTest() {
    if (cableTest_.value("running").toBool()) {
        return;
    }
    // Playback side: the processed output if it is virtual, else the suggested virtual microphone.
    // Recording side: VB-CABLE's "CABLE Output" for its "CABLE Input"; otherwise a device the user marked
    // as virtual, else any other virtual recording device.
    const AudioDeviceInfo* playback = findDevice(outputDeviceInfos_, settings_.engine.outputDeviceId);
    if (playback == nullptr || !playback->isVirtualCable) {
        playback = suggestedOutput();
    }
    std::wstring cableInput = playback != nullptr ? playback->id : std::wstring();
    std::wstring cableOutput;
    const bool vbCable = playback != nullptr && containsCi(playback->name, L"cable input");
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (!d.isVirtualCable) {
            continue;
        }
        if (vbCable ? containsCi(d.name, L"cable output") : isUserVirtual(d.id)) {
            cableOutput = d.id;
            break;
        }
        if (!vbCable && cableOutput.empty()) {
            cableOutput = d.id;
        }
    }
    QVariantMap result;
    result["hasResult"] = true;
    if (cableInput.empty() || cableOutput.empty()) {
        result["running"] = false;
        result["ok"] = false;
        result["message"] = QStringLiteral("A virtual microphone needs a playback side and a recording side, and both were not "
                                           "found. Install VB-CABLE and reboot, or mark both sides of your own virtual "
                                           "microphone in Setup, then refresh devices.");
        cableTest_ = result;
        emit cableTestChanged();
        return;
    }

    cableTest_["running"] = true;
    cableTest_["message"] = QStringLiteral("Testing... (the audio engine pauses for about 4 seconds)");
    emit cableTestChanged();

    LatencyTestOptions options;
    options.outputDeviceId = cableInput;
    options.inputDeviceId = cableOutput;
    supervisor_->runWithEngineStopped([this, options] {
        const LatencyTestResult r = runLoopbackLatencyTest(options);
        QMetaObject::invokeMethod(
            this,
            [this, r] {
                QVariantMap m;
                m["running"] = false;
                m["hasResult"] = true;
                m["ok"] = r.ok;
                m["medianMs"] = round2(r.medianMs);
                m["minMs"] = round2(r.minMs);
                m["maxMs"] = round2(r.maxMs);
                m["detected"] = r.pulsesDetected;
                m["sent"] = r.pulsesSent;
                m["message"] = r.ok ? QStringLiteral("Virtual cable works. Measured cable latency: %1 ms (median of %2 pulses).")
                                          .arg(round2(r.medianMs))
                                          .arg(r.pulsesDetected)
                                    : qs(r.error);
                cableTest_ = m;
                logInfo("Virtual cable test: " + (r.ok ? "OK, median " + std::to_string(r.medianMs) + " ms" : r.error));
                emit cableTestChanged();
            },
            Qt::QueuedConnection);
    });
}

void EngineController::openLogFolder() {
    const QFileInfo info(logFilePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void EngineController::copyToClipboard(const QString& text) {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString EngineController::userGuide() const {
    QFile file(QStringLiteral(":/docs/user-guide.md"));
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("The user guide is missing from this build.");
    }
    return QString::fromUtf8(file.readAll());
}

QString EngineController::diagnosticsReport() const {
    // Contains configuration, hardware and counters only - never audio or recordings.
    QStringList lines;
    lines << QStringLiteral("VoiceStudio diagnostics");
    lines << QStringLiteral("Version: %1").arg(QCoreApplication::applicationVersion());
    lines << QStringLiteral("Engine: %1 - %2").arg(engineState(), engineMessage());
    if (!engineError().isEmpty()) {
        lines << QStringLiteral("Last error: %1").arg(engineError());
    }
    lines << QStringLiteral("CPU: %1 (%2 cores / %3 threads)")
                 .arg(qs(hardwareInfo_.cpu.brand))
                 .arg(hardwareInfo_.cpu.physicalCores)
                 .arg(hardwareInfo_.cpu.logicalProcessors);
    for (const GpuInfo& g : hardwareInfo_.gpus) {
        lines << QStringLiteral("Accelerator %1: %2 (%3, DXGI %4)").arg(g.index).arg(qs(g.name), qs(g.id)).arg(g.dxgiIndex);
    }
    for (const std::string& ignored : hardwareInfo_.ignoredAdapters) {
        lines << QStringLiteral("Ignored adapter: %1").arg(qs(ignored));
    }
    {
        const IsolationStatus iso = supervisor_->engine().isolator().status();
        lines << QStringLiteral("Voice isolation processor: %1 (choice: %2)")
                     .arg(iso.processorName.empty() ? QStringLiteral("-") : qs(iso.processorName), selectedBackend_);
        for (const ProcessorTrial& t : iso.trials) {
            lines << QStringLiteral("  %1: %2").arg(qs(t.name),
                                                     t.error.empty() ? QStringLiteral("K=%1 p50 %2 ms p95 %3 ms%4")
                                                                           .arg(t.framesPerRun)
                                                                           .arg(t.p50Ms, 0, 'f', 2)
                                                                           .arg(t.p95Ms, 0, 'f', 2)
                                                                           .arg(t.realTime ? "" : " (too slow)")
                                                                     : qs(t.error));
        }
    }
    const EngineRuntimeInfo& i = status_.info;
    lines << QStringLiteral("Input: %1 [%2] period %3 ms, buffer %4 ms%5")
                 .arg(qs(i.inputName), qs(i.capture.format.describe()))
                 .arg(i.capture.periodMs)
                 .arg(i.capture.bufferMs)
                 .arg(i.capture.usedAudioClient3 ? QStringLiteral(", IAudioClient3") : QString());
    lines << QStringLiteral("Output: %1 [%2] period %3 ms, target %4 ms %5")
                 .arg(qs(i.outputName), qs(i.output.format.describe()))
                 .arg(i.output.periodMs)
                 .arg(i.outputTargetMs)
                 .arg(qs(i.outputError));
    for (auto it = stats_.cbegin(); it != stats_.cend(); ++it) {
        lines << QStringLiteral("%1: %2").arg(it.key(), it.value().toString());
    }
    return lines.join('\n');
}

QVariantMap EngineController::buildHardwareMap() const {
    QVariantMap hw;
    const CpuInfo& c = hardwareInfo_.cpu;
    QVariantMap cpu;
    cpu["vendor"] = qs(c.vendor);
    cpu["brand"] = qs(c.brand);
    cpu["physicalCores"] = c.physicalCores;
    cpu["logicalProcessors"] = c.logicalProcessors;
    QStringList simd;
    if (c.sse41) simd << "SSE4.1";
    if (c.avx) simd << "AVX";
    if (c.avx2) simd << "AVX2";
    if (c.fma) simd << "FMA";
    if (c.avx512f) simd << "AVX-512F";
    cpu["simd"] = simd.join(", ");
    cpu["totalMemoryGb"] = round1(static_cast<double>(c.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    cpu["availableMemoryGb"] = round1(static_cast<double>(c.availableMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    hw["cpu"] = cpu;

    QVariantList gpus;
    for (const GpuInfo& g : hardwareInfo_.gpus) {
        QVariantMap m;
        m["index"] = g.index;
        m["id"] = qs(g.id);
        m["name"] = qs(g.name);
        m["vendor"] = qs(g.vendorName);
        m["kind"] = g.kind == AcceleratorKind::DiscreteGpu ? QStringLiteral("discrete")
                    : g.kind == AcceleratorKind::IntegratedGpu ? QStringLiteral("integrated") : QStringLiteral("npu");
        m["dedicatedMemoryMb"] = static_cast<double>(g.dedicatedVideoMemory / (1024 * 1024));
        gpus.append(m);
    }
    hw["gpus"] = gpus;
    QStringList ignored;
    for (const std::string& i : hardwareInfo_.ignoredAdapters) {
        ignored << qs(i);
    }
    hw["ignoredAdapters"] = ignored;

    QVariantList backends;
    QVariantMap autoEntry;
    autoEntry["id"] = QStringLiteral("auto");
    autoEntry["name"] = QStringLiteral("Automatic (recommended)");
    autoEntry["available"] = true;
    autoEntry["kind"] = QStringLiteral("auto");
    autoEntry["detail"] = QStringLiteral("Measures the CPU and every usable graphics card on this PC when your voice is "
                                         "prepared, and uses the one with the lowest delay that keeps up.");
    backends.append(autoEntry);
    for (const ProcessingBackend& b : hardwareInfo_.backends) {
        QVariantMap m;
        m["id"] = qs(b.id);
        m["name"] = qs(b.displayName);
        m["available"] = b.available;
        m["kind"] = b.kind == ProcessorKind::Cpu ? QStringLiteral("cpu")
                    : b.kind == ProcessorKind::Gpu ? QStringLiteral("gpu") : QStringLiteral("npu");
        m["detail"] = qs(b.detail);
        backends.append(m);
    }
    hw["backends"] = backends;
    return hw;
}

// ---- Persistence ------------------------------------------------------------------------------

void EngineController::scheduleSave() {
    saveTimer_.start();
}

void EngineController::saveNow() {
    settings_.captureParams(params_);
    QString error;
    if (!SettingsStore::save(settingsPath_, settings_, &error)) {
        logError("Saving settings failed: " + error.toStdString());
    }
}

} // namespace vs
