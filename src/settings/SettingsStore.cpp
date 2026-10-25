#include "settings/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace vs {

namespace {

QString wide(const std::wstring& s) {
    return QString::fromStdWString(s);
}

double clampedNumber(const QJsonObject& o, const char* key, double fallback, double lo, double hi) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) {
        return fallback;
    }
    const double d = v.toDouble();
    return std::isfinite(d) ? std::clamp(d, lo, hi) : fallback;
}

bool boolean(const QJsonObject& o, const char* key, bool fallback) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

std::wstring wideString(const QJsonObject& o, const char* key) {
    return o.value(QLatin1String(key)).toString().toStdWString();
}

} // namespace

AppSettings::AppSettings() {
    for (const ParamSpec& spec : paramSpecs()) {
        params[static_cast<std::size_t>(spec.id)] = spec.defaultValue;
    }
}

void AppSettings::captureParams(const EngineParameters& source) {
    for (const ParamSpec& spec : paramSpecs()) {
        params[static_cast<std::size_t>(spec.id)] = source.get(spec.id);
    }
    effectOrder = source.effectOrder();
}

void AppSettings::applyParams(EngineParameters& target) const {
    for (const ParamSpec& spec : paramSpecs()) {
        target.set(spec.id, params[static_cast<std::size_t>(spec.id)]);
    }
    target.setEffectOrder(effectOrder);
}

QString SettingsStore::defaultPath() {
    return QString::fromStdWString((appDataDirectory() / L"settings.json").wstring());
}

QByteArray SettingsStore::toJson(const AppSettings& s) {
    QJsonObject root;
    root["version"] = AppSettings::kCurrentVersion;

    QJsonObject engine;
    engine["inputDeviceId"] = wide(s.engine.inputDeviceId);
    engine["inputFollowDefault"] = s.engine.inputFollowDefault;
    engine["outputDeviceId"] = wide(s.engine.outputDeviceId);
    engine["outputFollowDefault"] = s.engine.outputFollowDefault;
    engine["monitorDeviceId"] = wide(s.engine.monitorDeviceId);
    engine["monitorEnabled"] = s.engine.monitorEnabled;
    engine["bufferMode"] = static_cast<int>(s.engine.bufferMode);
    engine["safeBufferMs"] = s.engine.safeBufferMs;
    engine["outputTargetMs"] = s.engine.outputTargetMs;
    engine["maxExtraMs"] = s.engine.maxExtraMs;
    engine["deadlineMs"] = s.engine.deadlineMs;
    engine["autoRecover"] = s.engine.autoRecover;
    engine["enabled"] = s.engineEnabled;
    root["engine"] = engine;

    QJsonObject params;
    for (const ParamSpec& spec : paramSpecs()) {
        params[QLatin1String(spec.key)] = static_cast<double>(s.params[static_cast<std::size_t>(spec.id)]);
    }
    root["params"] = params;

    QJsonArray order;
    for (EffectSlot slot : s.effectOrder) {
        order.append(QLatin1String(effectSlotKey(slot)));
    }
    root["effectOrder"] = order;

    root["activePreset"] = QString::fromStdString(s.activePreset);
    root["performanceMode"] = QString::fromStdString(s.performanceMode);
    root["logLevel"] = static_cast<int>(s.logLevel);
    root["advancedUi"] = s.advancedUi;
    root["closeToTray"] = s.closeToTray;
    root["virtualMicAdopted"] = s.virtualMicAdopted;
    root["processor"] = QString::fromStdString(s.processor);
    QJsonArray virtualIds;
    for (const std::wstring& id : s.virtualDeviceIds) {
        virtualIds.append(wide(id));
    }
    root["virtualDeviceIds"] = virtualIds;

    QJsonObject custom;
    for (const auto& [name, values] : s.customPresets) {
        QJsonObject preset;
        for (const auto& [key, value] : values) {
            preset[QString::fromStdString(key)] = static_cast<double>(value);
        }
        custom[QString::fromStdString(name)] = preset;
    }
    root["customPresets"] = custom;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool SettingsStore::fromJson(const QByteArray& json, AppSettings& out, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Settings file is not valid JSON: %1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject root = doc.object();
    const int version = root.value("version").toInt(0);
    if (version < 1 || version > AppSettings::kCurrentVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported settings version %1").arg(version);
        }
        return false;
    }
    // Future migrations: if (version < 2) { ... } before reading fields.

    AppSettings s; // start from defaults; every field below is validated individually
    const QJsonObject engine = root.value("engine").toObject();
    s.engine.inputDeviceId = wideString(engine, "inputDeviceId");
    s.engine.inputFollowDefault = boolean(engine, "inputFollowDefault", true);
    s.engine.outputDeviceId = wideString(engine, "outputDeviceId");
    s.engine.outputFollowDefault = boolean(engine, "outputFollowDefault", false);
    s.engine.monitorDeviceId = wideString(engine, "monitorDeviceId");
    s.engine.monitorEnabled = boolean(engine, "monitorEnabled", false);
    s.engine.bufferMode = static_cast<BufferMode>(static_cast<int>(clampedNumber(engine, "bufferMode", 1, 0, 2)));
    s.engine.safeBufferMs = clampedNumber(engine, "safeBufferMs", 30.0, 10.0, 200.0);
    s.engine.outputTargetMs = clampedNumber(engine, "outputTargetMs", 12.0, 2.0, 200.0);
    s.engine.maxExtraMs = clampedNumber(engine, "maxExtraMs", 40.0, 10.0, 500.0);
    s.engine.deadlineMs = clampedNumber(engine, "deadlineMs", 5.0, 0.5, 10.0);
    s.engine.autoRecover = boolean(engine, "autoRecover", true);
    s.engineEnabled = boolean(engine, "enabled", true);
    if (!s.engine.inputFollowDefault && s.engine.inputDeviceId.empty()) {
        s.engine.inputFollowDefault = true;
    }

    const QJsonObject params = root.value("params").toObject();
    for (const ParamSpec& spec : paramSpecs()) {
        const QJsonValue v = params.value(QLatin1String(spec.key));
        if (v.isDouble()) {
            s.params[static_cast<std::size_t>(spec.id)] = clampParam(spec.id, static_cast<float>(v.toDouble()));
        }
    }

    const QJsonArray order = root.value("effectOrder").toArray();
    if (order.size() == static_cast<qsizetype>(kEffectSlotCount)) {
        EffectOrder parsed{};
        bool valid = true;
        for (qsizetype i = 0; i < order.size(); ++i) {
            valid = valid && effectSlotFromKey(order[i].toString().toStdString(), parsed[static_cast<std::size_t>(i)]);
        }
        if (valid && isValidEffectOrder(parsed)) {
            s.effectOrder = parsed;
        }
    }

    s.activePreset = root.value("activePreset").toString(QString::fromStdString(s.activePreset)).toStdString();
    s.performanceMode = root.value("performanceMode").toString(QString::fromStdString(s.performanceMode)).toStdString();
    s.logLevel = static_cast<LogLevel>(std::clamp(root.value("logLevel").toInt(1), 0, 4));
    s.advancedUi = root.value("advancedUi").toBool(false);
    s.closeToTray = root.value("closeToTray").toBool(true);
    s.virtualMicAdopted = root.value("virtualMicAdopted").toBool(false);
    s.processor = root.value("processor").toString(QStringLiteral("auto")).toStdString();
    if (s.processor.empty() || s.processor.size() > 64) {
        s.processor = "auto";
    }
    for (const QJsonValue& v : root.value("virtualDeviceIds").toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty() && s.virtualDeviceIds.size() < 64) {
            s.virtualDeviceIds.push_back(id.toStdWString());
        }
    }

    const QJsonObject custom = root.value("customPresets").toObject();
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        const QString name = it.key().trimmed().left(64);
        if (name.isEmpty() || !it.value().isObject()) {
            continue;
        }
        std::map<std::string, float> values;
        const QJsonObject preset = it.value().toObject();
        for (auto p = preset.begin(); p != preset.end(); ++p) {
            const std::string key = p.key().toStdString();
            const ParamSpec* spec = findParam(key);
            if (spec != nullptr && p.value().isDouble()) {
                values[key] = clampParam(spec->id, static_cast<float>(p.value().toDouble()));
            }
        }
        s.customPresets[name.toStdString()] = std::move(values);
    }

    out = std::move(s);
    return true;
}

bool SettingsStore::load(const QString& path, AppSettings& out, QString* error) {
    QFile file(path);
    if (!file.exists()) {
        out = AppSettings{};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        out = AppSettings{};
        if (error) {
            *error = QStringLiteral("Cannot read settings: %1").arg(file.errorString());
        }
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (!fromJson(data, out, error)) {
        out = AppSettings{};
        QFile::remove(path + ".bad");
        QFile::copy(path, path + ".bad");
        return false;
    }
    return true;
}

bool SettingsStore::save(const QString& path, const AppSettings& settings, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(toJson(settings));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace vs
