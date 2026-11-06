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

TEST(Presets, ApplyKeepsVoiceCharacter) {
    EngineParameters p;
    applyVoiceLook(voiceLooks()[1], p);
    applyPreset(*findBuiltInPreset("Gaming"), p);
    EXPECT_TRUE(p.getBool(ParamId::PitchEnabled));
    EXPECT_EQ(matchVoiceLook(p), &voiceLooks()[1]);
}

TEST(Presets, VoiceLooks) {
    EngineParameters p;
    EXPECT_EQ(matchVoiceLook(p), &voiceLooks().front()); // character off: Natural
    for (const VoiceLook& look : voiceLooks()) {
        applyVoiceLook(look, p);
        EXPECT_EQ(matchVoiceLook(p), &look) << look.key;
    }
    applyVoiceLook(voiceLooks().front(), p);
    EXPECT_FALSE(p.getBool(ParamId::PitchEnabled));
    p.set(ParamId::PitchEnabled, 1.f);
    p.set(ParamId::PitchSemitones, -5.f);
    EXPECT_EQ(matchVoiceLook(p), nullptr); // custom
}

TEST(Presets, VoiceValues) {
    EngineParameters p;
    EXPECT_TRUE(currentVoiceValues(p).natural());
    applyVoiceValues({-2.0f, 1.0f, 3.0f}, p);
    EXPECT_TRUE(p.getBool(ParamId::PitchEnabled));
    EXPECT_TRUE(currentVoiceValues(p).matches({-2.0f, 1.0f, 3.0f}));
    applyVoiceValues({}, p); // all zero: character off, no delay added
    EXPECT_FALSE(p.getBool(ParamId::PitchEnabled));
    EXPECT_TRUE(currentVoiceValues(p).natural());
    // Every look has its own values (no two tiles select the same voice); only Natural is natural.
    const auto& looks = voiceLooks();
    for (std::size_t i = 0; i < looks.size(); ++i) {
        EXPECT_EQ(voiceValues(looks[i]).natural(), i == 0) << looks[i].key;
        for (std::size_t j = i + 1; j < looks.size(); ++j) {
            EXPECT_FALSE(voiceValues(looks[i]).matches(voiceValues(looks[j]))) << looks[i].key << " " << looks[j].key;
        }
    }
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
