#include "settings/Presets.h"

#include <gtest/gtest.h>

using namespace vs;

TEST(Presets, RequiredBuiltInsExist) {
    for (const char* name : {"Interview", "Natural Voice", "Gaming", "Streaming", "Noise Reduction Maximum"}) {
        EXPECT_NE(findBuiltInPreset(name), nullptr) << name;
    }
}

TEST(Presets, ApplyKeepsPerSetupValues) {
    EngineParameters p;
    p.set(ParamId::InputGainDb, 7.0f);
    p.set(ParamId::MonitorGainDb, -12.0f);
    p.set(ParamId::TrebleDb, 9.0f);
    applyPreset(*findBuiltInPreset("Interview"), p);
    EXPECT_FLOAT_EQ(p.get(ParamId::InputGainDb), 7.0f);
    EXPECT_FLOAT_EQ(p.get(ParamId::MonitorGainDb), -12.0f);
    EXPECT_FLOAT_EQ(p.get(ParamId::TrebleDb), 0.0f); // reset to default by the preset
    EXPECT_FLOAT_EQ(p.get(ParamId::ClarityDb), 2.0f);
}

TEST(Presets, CustomRoundTrip) {
    EngineParameters a;
    a.set(ParamId::BassDb, 4.5f);
    a.set(ParamId::GateThresholdDb, -42.0f);
    const auto values = capturePresetValues(a);
    EXPECT_EQ(values.count("inputGainDb"), 0u);

    EngineParameters b;
    applyPresetValues(values, b);
    EXPECT_FLOAT_EQ(b.get(ParamId::BassDb), 4.5f);
    EXPECT_FLOAT_EQ(b.get(ParamId::GateThresholdDb), -42.0f);
}

TEST(Presets, PerformanceProfilesCoverAllModes) {
    EXPECT_NE(findPerformanceProfile("ultraLowLatency"), nullptr);
    EXPECT_NE(findPerformanceProfile("balanced"), nullptr);
    EXPECT_NE(findPerformanceProfile("maximumNoiseReduction"), nullptr);
    EXPECT_NE(findPerformanceProfile("custom"), nullptr);
    EXPECT_EQ(findPerformanceProfile("ultraLowLatency")->bufferMode, BufferMode::LowLatency);
}
