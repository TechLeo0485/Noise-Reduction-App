#include "app/EngineController.h"

#include "core/DspMath.h"
#include "diagnostics/LoopbackLatencyTester.h"
#include "diagnostics/Logger.h"
#include "diagnostics/VoicePlayback.h"
#include "settings/Presets.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cwctype>

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

    hardwareInfo_ = detectHardware();
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

    supervisor_->applyConfig(settings_.engine);
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
    inputDevices_ = toVariantList(inputDeviceInfos_);
    outputDevices_ = toVariantList(outputDeviceInfos_);
    cableDetected_ = std::any_of(outputDeviceInfos_.begin(), outputDeviceInfos_.end(),
                                 [](const AudioDeviceInfo& d) { return d.isVirtualCable; });
    emit devicesChanged();
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

const AudioDeviceInfo* EngineController::suggestedOutput() const {
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (d.isVirtualCable && containsCi(d.name, L"cable input")) {
            return &d;
        }
    }
    return nullptr;
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
    s["outputName"] = info.outputRunning ? qs(info.outputName) : QString();
    s["outputOk"] = outputIsCable;
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

void EngineController::autoSelectCableOutput() {
    if (!settings_.engine.outputDeviceId.empty() || settings_.engine.outputFollowDefault) {
        return;
    }
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (d.isVirtualCable && containsCi(d.name, L"cable input")) {
            settings_.engine.outputDeviceId = d.id;
            logInfo("Auto-selected virtual cable output: " + toUtf8(d.name));
            scheduleSave();
            return;
        }
    }
}

void EngineController::onDeviceEvent(const DeviceEvent& event) {
    refreshDevices();
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
    const std::wstring inputId =
        settings_.engine.inputFollowDefault ? deviceManager_->defaultDeviceId(DeviceFlow::Capture) : settings_.engine.inputDeviceId;
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (d.id == inputId && d.isVirtualCable) {
            warning = QStringLiteral("The microphone input is a virtual audio device (%1), not a physical microphone. "
                                     "That adds latency and can loop audio back on itself. Select your physical "
                                     "microphone in Audio Devices.")
                          .arg(qs(d.name));
        }
    }
    if (warning.isEmpty() && !settings_.engine.outputDeviceId.empty()) {
        for (const AudioDeviceInfo& d : outputDeviceInfos_) {
            if (d.id == settings_.engine.outputDeviceId && !d.isVirtualCable) {
                warning = QStringLiteral("Processed output is set to \"%1\", a real speaker or headphone. Other apps will "
                                         "not receive your voice. Select \"CABLE Input\" as the processed output and use "
                                         "\"Listen live\" to hear yourself.")
                              .arg(qs(d.name));
            }
        }
    }
    if (warning.isEmpty() && settings_.engine.outputDeviceId.empty() && !settings_.engine.outputFollowDefault) {
        warning = cableDetected_ ? QStringLiteral("Select \"CABLE Input\" as the processed output so other apps can use "
                                                  "\"CABLE Output\" as their microphone.")
                                 : QStringLiteral("No virtual audio cable found. Install VB-CABLE (see Audio Devices) to "
                                                  "use processed audio in Discord, Zoom or Teams.");
    }
    if (warning != routingWarning_) {
        routingWarning_ = warning;
        emit configChanged();
    }
}

// ---- Engine config ------------------------------------------------------------------------

void EngineController::pushConfig() {
    supervisor_->applyConfig(settings_.engine);
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
    settings_.engine.outputTargetMs = std::clamp(ms, 2.0, 200.0);
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
    for (const ProcessingBackend& b : hardwareInfo_.backends) {
        if (qs(b.id) == id && !b.available) {
            return;
        }
    }
    if (id != selectedBackend_) {
        selectedBackend_ = id;
        emit configChanged();
    }
}

void EngineController::restartEngine() {
    supervisor_->requestRestart();
}

void EngineController::resetToDefaults() {
    const auto customPresets = settings_.customPresets;
    settings_ = AppSettings{};
    settings_.customPresets = customPresets; // user-created presets are kept
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
    if (isPresetScoped(spec->id) && !presetModified_) {
        presetModified_ = true;
        emit presetsChanged();
    }
    emit paramsChanged();
    scheduleSave();
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
    activeSuppressor_ = QString::fromLatin1(
        suppressorKindName(static_cast<SuppressorKind>(m.activeSuppressor.load(std::memory_order_relaxed))));
    emit metersChanged();
    pollVoiceCheck();

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

    s["processCpuPercent"] = proc.cpuPercent >= 0.0 ? round1(proc.cpuPercent) : -1.0;
    s["workingSetMb"] = round1(proc.workingSetMb);
    s["privateMb"] = round1(proc.privateBytesMb);
    s["restartCount"] = static_cast<qulonglong>(status_.restartCount);
    stats_ = s;
    emit statsChanged();
}

void EngineController::resetStatistics() {
    statsCollector_.clear();
}

// ---- Diagnostics ------------------------------------------------------------------------------

void EngineController::runVirtualCableTest() {
    if (cableTest_.value("running").toBool()) {
        return;
    }
    std::wstring cableInput;
    std::wstring cableOutput;
    for (const AudioDeviceInfo& d : outputDeviceInfos_) {
        if (d.isVirtualCable && containsCi(d.name, L"cable input")) {
            cableInput = d.id;
        }
    }
    for (const AudioDeviceInfo& d : inputDeviceInfos_) {
        if (d.isVirtualCable && containsCi(d.name, L"cable output")) {
            cableOutput = d.id;
        }
    }
    QVariantMap result;
    result["hasResult"] = true;
    if (cableInput.empty() || cableOutput.empty()) {
        result["running"] = false;
        result["ok"] = false;
        result["message"] = QStringLiteral("VB-CABLE endpoints \"CABLE Input\" (playback) and \"CABLE Output\" (recording) "
                                           "were not both found. Install VB-CABLE and reboot, then refresh devices.");
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
        lines << QStringLiteral("GPU %1: %2").arg(g.index).arg(qs(g.name));
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
        m["name"] = qs(g.name);
        m["vendor"] = qs(g.vendorName);
        m["dedicatedMemoryMb"] = static_cast<double>(g.dedicatedVideoMemory / (1024 * 1024));
        m["software"] = g.isSoftwareAdapter;
        gpus.append(m);
    }
    hw["gpus"] = gpus;

    QVariantList backends;
    QVariantMap autoEntry;
    autoEntry["id"] = QStringLiteral("auto");
    autoEntry["name"] = QStringLiteral("Auto");
    autoEntry["available"] = true;
    autoEntry["detail"] = QStringLiteral("Picks the fastest reliable backend. Only the CPU backend exists in this build.");
    backends.append(autoEntry);
    for (const ProcessingBackend& b : hardwareInfo_.backends) {
        QVariantMap m;
        m["id"] = qs(b.id);
        m["name"] = qs(b.displayName);
        m["available"] = b.available;
        m["detail"] = qs(b.detail);
        backends.append(m);
    }
    hw["backends"] = backends;
    hw["gpuUtilization"] = QStringLiteral("Unavailable (not measured in this build)");
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
