#include "settings/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace vs;

TEST(SettingsStore, JsonRoundTrip) {
    AppSettings s;
    s.engine.inputDeviceId = L"{0.0.1.00000000}.{abc}";
    s.engine.inputFollowDefault = false;
    s.engine.outputDeviceId = L"{0.0.0.00000000}.{cable}";
    s.engine.bufferMode = BufferMode::LowLatency;
    s.engine.outputTargetMs = 8.0;
    s.params[static_cast<std::size_t>(ParamId::BassDb)] = 3.5f;
    s.effectOrder = {EffectSlot::Compressor, EffectSlot::Equalizer, EffectSlot::Pitch};
    s.activePreset = "Gaming";
    s.customPresets["Mine"] = {{"bassDb", 2.0f}, {"trebleDb", -1.0f}};
    s.advancedUi = true;

    AppSettings loaded;
    ASSERT_TRUE(SettingsStore::fromJson(SettingsStore::toJson(s), loaded));
    EXPECT_TRUE(loaded.advancedUi);
    EXPECT_FALSE(AppSettings{}.advancedUi); // simple UI is the default
    EXPECT_EQ(loaded.engine.inputDeviceId, s.engine.inputDeviceId);
    EXPECT_FALSE(loaded.engine.inputFollowDefault);
    EXPECT_EQ(loaded.engine.outputDeviceId, s.engine.outputDeviceId);
    EXPECT_EQ(loaded.engine.bufferMode, BufferMode::LowLatency);
    EXPECT_DOUBLE_EQ(loaded.engine.outputTargetMs, 8.0);
    EXPECT_FLOAT_EQ(loaded.params[static_cast<std::size_t>(ParamId::BassDb)], 3.5f);
    EXPECT_EQ(loaded.effectOrder, s.effectOrder);
    EXPECT_EQ(loaded.activePreset, "Gaming");
    ASSERT_EQ(loaded.customPresets.count("Mine"), 1u);
    EXPECT_FLOAT_EQ(loaded.customPresets["Mine"]["trebleDb"], -1.0f);
}

TEST(SettingsStore, ValuesAreValidatedOnLoad) {
    const QByteArray json = R"({
        "version": 1,
        "engine": { "outputTargetMs": 100000, "bufferMode": 7, "inputFollowDefault": false, "inputDeviceId": "" },
        "params": { "bassDb": 99, "masterMute": 0.2, "unknownKey": 5 },
        "effectOrder": ["eq", "eq", "pitch"]
    })";
    AppSettings s;
    ASSERT_TRUE(SettingsStore::fromJson(json, s));
    EXPECT_DOUBLE_EQ(s.engine.outputTargetMs, 60.0); // part of the 300 ms delay budget
    EXPECT_EQ(s.engine.bufferMode, BufferMode::Safe);
    EXPECT_TRUE(s.engine.inputFollowDefault); // no device ID -> follow default
    EXPECT_FLOAT_EQ(s.params[static_cast<std::size_t>(ParamId::BassDb)], 12.0f);
    EXPECT_FLOAT_EQ(s.params[static_cast<std::size_t>(ParamId::MasterMute)], 0.0f);
    EXPECT_EQ(s.effectOrder, defaultEffectOrder()); // duplicate entries rejected
}

TEST(SettingsStore, RejectsCorruptOrFutureFiles) {
    AppSettings s;
    QString error;
    EXPECT_FALSE(SettingsStore::fromJson("{ not json", s, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(SettingsStore::fromJson(R"({"version": 99})", s, &error));
    EXPECT_FALSE(SettingsStore::fromJson(R"([1,2,3])", s, &error));
}

TEST(SettingsStore, PersistsToDiskAndRecoversFromCorruption) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("settings.json");

    AppSettings missing;
    EXPECT_TRUE(SettingsStore::load(path, missing)); // missing file -> defaults

    AppSettings s;
    s.activePreset = "Streaming";
    ASSERT_TRUE(SettingsStore::save(path, s));
    AppSettings loaded;
    ASSERT_TRUE(SettingsStore::load(path, loaded));
    EXPECT_EQ(loaded.activePreset, "Streaming");

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("garbage");
    f.close();
    AppSettings recovered;
    EXPECT_FALSE(SettingsStore::load(path, recovered));
    EXPECT_EQ(recovered.activePreset, AppSettings{}.activePreset);
    EXPECT_TRUE(QFile::exists(path + ".bad"));
}

TEST(SettingsStore, ParamsApplyToEngine) {
    AppSettings s;
    s.params[static_cast<std::size_t>(ParamId::GateThresholdDb)] = -33.0f;
    EngineParameters p;
    s.applyParams(p);
    EXPECT_FLOAT_EQ(p.get(ParamId::GateThresholdDb), -33.0f);
    p.set(ParamId::TrebleDb, 4.0f);
    s.captureParams(p);
    EXPECT_FLOAT_EQ(s.params[static_cast<std::size_t>(ParamId::TrebleDb)], 4.0f);
}
