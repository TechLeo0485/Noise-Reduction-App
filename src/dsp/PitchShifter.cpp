#include "dsp/PitchShifter.h"

#include "core/Parameters.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

namespace {
// 35 ms analysis blocks (~29 Hz resolution, enough for low voices) with 4x overlap: ~35 ms latency.
constexpr float kBlockSeconds = 0.035f;
constexpr float kLowCutHz = 90.0f;
constexpr float kAntiBoomHz = 150.0f;
constexpr float kAntiBoomMaxDb = 7.0f;
constexpr float kPresenceHz = 3200.0f;
} // namespace

struct PitchShifter::Engine {
    // Fixed seed: no std::random_device (and identical output run to run).
    signalsmith::stretch::SignalsmithStretch<float> stretch{1};
};

PitchShifter::PitchShifter() = default;
PitchShifter::~PitchShifter() = default;

void PitchShifter::prepare(float sampleRate, std::size_t maxBlockSize) {
    sampleRate_ = sampleRate;
    engine_ = std::make_unique<Engine>();
    const int block = static_cast<int>(std::lround(kBlockSeconds * sampleRate));
    engine_->stretch.configure(1, block, block / 4);
    latency_ = engine_->stretch.inputLatency() + engine_->stretch.outputLatency();
    input_.assign(std::max<std::size_t>(maxBlockSize, 1), 0.0f);
    lowCut_.setCoeffs(designHighPass(sampleRate, kLowCutHz, 0.7071f));
    dirty_ = true;
    reset();
}

void PitchShifter::reset() noexcept {
    if (engine_) {
        engine_->stretch.reset();
    }
    lowCut_.reset();
    antiBoom_.reset();
    presence_.reset();
    active_ = false;
    warmup_ = 0;
    mix_ = 0.0f;
}

void PitchShifter::configure(bool enabled, float semitones, float bodySemitones, float presenceDb) noexcept {
    if (enabled != enabled_ || semitones != semitones_ || bodySemitones != body_ || presenceDb != presenceDb_) {
        enabled_ = enabled;
        semitones_ = semitones;
        body_ = bodySemitones;
        presenceDb_ = presenceDb;
        dirty_ = true;
    }
}

void PitchShifter::updateParameters(const EngineParameters& p) noexcept {
    configure(p.getBool(ParamId::PitchEnabled), p.get(ParamId::PitchSemitones), p.get(ParamId::PitchBodySt),
              p.get(ParamId::PitchPresenceDb));
}

int PitchShifter::latencySamples() const noexcept {
    return mix_ > 0.0f ? latency_ : 0;
}

void PitchShifter::applySettings() noexcept {
    engine_->stretch.setTransposeSemitones(semitones_);
    // Formant compensation keeps the formants in place under the transposition; Body then moves them on purpose.
    engine_->stretch.setFormantSemitones(-body_, true);
    presence_.setCoeffs(designPeaking(sampleRate_, kPresenceHz, 0.8f, presenceDb_));
    // A lower pitch and lower formants both pile energy up below ~150 Hz, which sounds boomy rather than strong.
    const float boomDb = std::clamp(-semitones_ + 0.5f * std::max(0.0f, body_), 0.0f, kAntiBoomMaxDb);
    antiBoom_.setCoeffs(designLowShelf(sampleRate_, kAntiBoomHz, -boomDb));
    dirty_ = false;
}

void PitchShifter::process(float* samples, std::size_t count) noexcept {
    if (!engine_ || count == 0) {
        return;
    }
    if (count > input_.size()) { // never in the chain (fixed frames); keeps direct callers safe
        for (std::size_t offset = 0; offset < count; offset += input_.size()) {
            process(samples + offset, std::min(input_.size(), count - offset));
        }
        return;
    }
    if (dirty_) {
        applySettings();
    }
    const bool wanted = enabled_ && (std::fabs(semitones_) > 0.01f || std::fabs(body_) > 0.01f ||
                                     std::fabs(presenceDb_) > 0.05f);
    if (!active_) {
        if (!wanted) {
            return;
        }
        // Start from a clean state; the dry signal plays until the shifter's output is valid.
        engine_->stretch.reset();
        lowCut_.reset();
        antiBoom_.reset();
        presence_.reset();
        active_ = true;
        warmup_ = latency_;
        mix_ = 0.0f;
    }

    float* dry = input_.data();
    std::memcpy(dry, samples, count * sizeof(float));
    const float* in[1] = {dry};
    float* out[1] = {samples};
    engine_->stretch.process(in, static_cast<int>(count), out, static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        samples[i] = presence_.process(antiBoom_.process(lowCut_.process(samples[i])));
    }

    const bool ready = warmup_ <= 0;
    warmup_ = std::max(0, warmup_ - static_cast<int>(count));
    const float target = wanted && ready ? 1.0f : 0.0f;
    const float step = (target - mix_) / static_cast<float>(count);
    float mix = mix_;
    for (std::size_t i = 0; i < count; ++i) {
        mix += step;
        samples[i] = dry[i] + mix * (samples[i] - dry[i]);
    }
    mix_ = target;
    if (!wanted && mix_ == 0.0f) {
        active_ = false;
    }
}

} // namespace vs
