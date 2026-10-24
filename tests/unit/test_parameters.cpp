#include "core/Parameters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>

using namespace vs;

TEST(Parameters, RegistryKeysAreUniqueAndDefaultsInRange) {
    std::set<std::string> keys;
    for (const ParamSpec& spec : paramSpecs()) {
        EXPECT_TRUE(keys.insert(spec.key).second) << "duplicate key " << spec.key;
        EXPECT_LE(spec.minValue, spec.defaultValue) << spec.key;
        EXPECT_GE(spec.maxValue, spec.defaultValue) << spec.key;
        EXPECT_EQ(findParam(spec.key), &spec);
    }
    EXPECT_EQ(findParam("doesNotExist"), nullptr);
}

TEST(Parameters, DefaultsAreApplied) {
    EngineParameters p;
    for (const ParamSpec& spec : paramSpecs()) {
        EXPECT_FLOAT_EQ(p.get(spec.id), spec.defaultValue) << spec.key;
    }
}

TEST(Parameters, SetClampsAndSnaps) {
    EngineParameters p;
    EXPECT_FLOAT_EQ(p.set(ParamId::InputGainDb, 100.0f), 24.0f);
    EXPECT_FLOAT_EQ(p.set(ParamId::InputGainDb, -100.0f), -24.0f);
    EXPECT_FLOAT_EQ(p.set(ParamId::MasterMute, 0.7f), 1.0f);
    EXPECT_TRUE(p.getBool(ParamId::MasterMute));
    EXPECT_FLOAT_EQ(p.set(ParamId::SuppressionMode, 1.4f), 1.0f);
    EXPECT_EQ(p.getInt(ParamId::SuppressionMode), 1);
    EXPECT_FLOAT_EQ(p.set(ParamId::BassDb, NAN), paramSpec(ParamId::BassDb).defaultValue);
}

TEST(Parameters, EffectOrderValidation) {
    EngineParameters p;
    EXPECT_EQ(p.effectOrder(), defaultEffectOrder());
    const EffectOrder custom{EffectSlot::Pitch, EffectSlot::Equalizer, EffectSlot::Compressor};
    p.setEffectOrder(custom);
    EXPECT_EQ(p.effectOrder(), custom);

    const EffectOrder duplicate{EffectSlot::Pitch, EffectSlot::Pitch, EffectSlot::Compressor};
    EXPECT_FALSE(isValidEffectOrder(duplicate));
    p.setEffectOrder(duplicate); // ignored
    EXPECT_EQ(p.effectOrder(), custom);

    EffectSlot slot{};
    EXPECT_TRUE(effectSlotFromKey("compressor", slot));
    EXPECT_EQ(slot, EffectSlot::Compressor);
    EXPECT_FALSE(effectSlotFromKey("reverb", slot));
}

TEST(Parameters, ResetRestoresDefaults) {
    EngineParameters p;
    p.set(ParamId::TrebleDb, 9.0f);
    p.setEffectOrder({EffectSlot::Pitch, EffectSlot::Compressor, EffectSlot::Equalizer});
    p.resetToDefaults();
    EXPECT_FLOAT_EQ(p.get(ParamId::TrebleDb), 0.0f);
    EXPECT_EQ(p.effectOrder(), defaultEffectOrder());
}
