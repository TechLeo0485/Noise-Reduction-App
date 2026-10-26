#include "dsp/VoiceStabilizer.h"

#include "core/DspMath.h"
#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

namespace {
constexpr float kSpeechFloorDb = -60.0f;      // quieter blocks are never speech
constexpr float kVadThreshold = 0.6f;
constexpr float kAboveNoiseDb = 12.0f;        // without voice activity: this far above the noise floor is speech
constexpr float kNoiseRiseDbPerSec = 3.0f;
constexpr float kLevelSeconds = 0.6f;         // speech level averaging
constexpr float kGainUpDbPerSec = 6.0f;       // the leveler raises slowly ...
constexpr float kGainDownDbPerSec = 12.0f;    // ... and lowers faster (leaning in is the louder surprise)
constexpr float kToneFastSeconds = 1.5f;
constexpr float kToneRefSeconds = 120.0f;     // the speaker's own balance
constexpr float kToneLearnSeconds = 5.0f;     // speech it takes to learn a first reference
constexpr float kToneSettleSeconds = 3.0f;    // no tone correction before this much speech
constexpr float kToneAmount = 0.8f;           // share of the deviation undone
constexpr float kToneDbPerSec = 3.0f;
constexpr float kShelfHz = 200.0f;
constexpr float kTiny = 1e-12f;

float powerDb(float power) noexcept {
    return 10.0f * std::log10(std::max(power, kTiny));
}

float smoothing(float blockSeconds, float timeConstantSeconds) noexcept {
    return 1.0f - std::exp(-blockSeconds / timeConstantSeconds);
}

float glide(float current, float target, float upStep, float downStep) noexcept {
    return target > current ? std::min(target, current + upStep) : std::max(target, current - downStep);
}

void track(float& value, float power, float coeff) noexcept {
    value = value <= 0.0f ? power : value + coeff * (power - value);
}
} // namespace

void VoiceStabilizer::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    lowBand_.setCoeffs(designLowPass(sampleRate, 250.0f, 0.7071f));
    midHigh_.setCoeffs(designHighPass(sampleRate, 500.0f, 0.7071f));
    midLow_.setCoeffs(designLowPass(sampleRate, 2000.0f, 0.7071f));
    reset();
}

void VoiceStabilizer::reset() noexcept {
    lowBand_.reset();
    midHigh_.reset();
    midLow_.reset();
    shelf_.reset();
    noiseFloorDb_ = -70.0f;
    speechPower_ = lowFast_ = midFast_ = lowRef_ = midRef_ = 0.0f;
    speechBlocks_ = 0;
    gainDb_ = toneDb_ = 0.0f;
    appliedToneDb_ = 1000.0f;
}

void VoiceStabilizer::updateParameters(const EngineParameters& params) noexcept {
    configure(params.getBool(ParamId::StabilizerEnabled));
}

bool VoiceStabilizer::isSpeech(float levelDb) noexcept {
    if (levelDb < kSpeechFloorDb) {
        return false;
    }
    if (voiceProbability_ >= 0.0f) {
        return voiceProbability_ >= kVadThreshold;
    }
    return levelDb > noiseFloorDb_ + kAboveNoiseDb;
}

void VoiceStabilizer::process(float* samples, std::size_t count) noexcept {
    if (count == 0) {
        return;
    }
    const float blockSeconds = static_cast<float>(count) / sampleRate_;

    // ---- Analysis of the incoming voice ----
    float energy = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        const float l = lowBand_.process(x);
        const float m = midLow_.process(midHigh_.process(x));
        energy += x * x;
        low += l * l;
        mid += m * m;
    }
    const auto n = static_cast<float>(count);
    const float power = energy / n;
    const float levelDb = powerDb(power);

    // Noise floor: follows quiet blocks quickly, rises slowly.
    if (levelDb < noiseFloorDb_) {
        noiseFloorDb_ += 0.5f * (levelDb - noiseFloorDb_);
    } else {
        noiseFloorDb_ += kNoiseRiseDbPerSec * blockSeconds;
    }
    noiseFloorDb_ = std::clamp(noiseFloorDb_, -100.0f, -20.0f);

    const bool speech = isSpeech(levelDb);
    if (speech) {
        ++speechBlocks_;
        track(speechPower_, power, smoothing(blockSeconds, kLevelSeconds));
        const float fast = smoothing(blockSeconds, kToneFastSeconds);
        track(lowFast_, low / n, fast);
        track(midFast_, mid / n, fast);
        const bool learning = static_cast<float>(speechBlocks_) * blockSeconds < kToneLearnSeconds;
        const float ref = learning ? fast : smoothing(blockSeconds, kToneRefSeconds);
        track(lowRef_, low / n, ref);
        track(midRef_, mid / n, ref);
    }

    // ---- Targets ----
    float gainTarget = 0.0f;
    float toneTarget = 0.0f;
    if (enabled_) {
        if (speechPower_ > 0.0f) {
            gainTarget = std::clamp(kTargetDb - powerDb(speechPower_), kMaxCutDb, kMaxBoostDb);
        }
        const bool settled = static_cast<float>(speechBlocks_) * blockSeconds >= kToneSettleSeconds;
        if (settled && midFast_ > kTiny && midRef_ > kTiny) {
            const float deviationDb = (powerDb(lowFast_) - powerDb(midFast_)) - (powerDb(lowRef_) - powerDb(midRef_));
            toneTarget = std::clamp(-kToneAmount * deviationDb, kMaxToneCutDb, kMaxToneBoostDb);
        }
    }

    // ---- Glide (only while speaking, so nothing moves on noise; always when switched off) ----
    const float oldGain = dbToLinear(gainDb_);
    if (speech || !enabled_) {
        gainDb_ = glide(gainDb_, gainTarget, kGainUpDbPerSec * blockSeconds, kGainDownDbPerSec * blockSeconds);
        toneDb_ = glide(toneDb_, toneTarget, kToneDbPerSec * blockSeconds, kToneDbPerSec * blockSeconds);
    }
    if (std::fabs(toneDb_ - appliedToneDb_) > 0.01f) {
        shelf_.setCoeffs(designLowShelf(sampleRate_, kShelfHz, toneDb_));
        appliedToneDb_ = toneDb_;
    }

    // ---- Apply ----
    if (appliedToneDb_ != 0.0f) {
        shelf_.processBlock(samples, count);
    } else {
        shelf_.reset();
    }
    applyGainRamp(samples, count, oldGain, dbToLinear(gainDb_));
}

} // namespace vs
