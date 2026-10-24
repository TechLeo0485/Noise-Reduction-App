#include "dsp/Equalizer.h"

#include "core/Parameters.h"

#include <cmath>

namespace vs {

void Equalizer::prepare(float sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    for (std::size_t b = 0; b < BandCount; ++b) {
        const float g = gainsDb_[b];
        gainsDb_[b] = 1000.0f; // force recompute
        setBandGain(static_cast<Band>(b), g);
    }
    reset();
}

void Equalizer::reset() noexcept {
    for (auto& f : filters_) {
        f.reset();
    }
}

void Equalizer::setBandGain(Band band, float gainDb) noexcept {
    if (gainsDb_[band] == gainDb) {
        return;
    }
    gainsDb_[band] = gainDb;
    const bool wasActive = active_[band];
    active_[band] = std::fabs(gainDb) > kBypassThresholdDb;
    if (active_[band] && !wasActive) {
        filters_[band].reset();
    }

    BiquadCoeffs c;
    switch (band) {
    case Bass: c = designLowShelf(sampleRate_, 120.0f, gainDb); break;
    case Warmth: c = designPeaking(sampleRate_, 250.0f, 0.9f, gainDb); break;
    case Mid: c = designPeaking(sampleRate_, 1000.0f, 0.8f, gainDb); break;
    case Clarity: c = designPeaking(sampleRate_, 3500.0f, 1.0f, gainDb); break;
    case Treble: c = designHighShelf(sampleRate_, 7000.0f, gainDb); break;
    default: break;
    }
    filters_[band].setCoeffs(c);
}

void Equalizer::updateParameters(const EngineParameters& p) noexcept {
    setBandGain(Bass, p.get(ParamId::BassDb));
    setBandGain(Warmth, p.get(ParamId::WarmthDb));
    setBandGain(Mid, p.get(ParamId::MidDb));
    setBandGain(Clarity, p.get(ParamId::ClarityDb));
    setBandGain(Treble, p.get(ParamId::TrebleDb));
}

void Equalizer::process(float* samples, std::size_t count) noexcept {
    for (std::size_t b = 0; b < BandCount; ++b) {
        if (active_[b]) {
            filters_[b].processBlock(samples, count);
        }
    }
}

float Equalizer::magnitudeAt(float frequency) const noexcept {
    float m = 1.0f;
    for (std::size_t b = 0; b < BandCount; ++b) {
        if (active_[b]) {
            m *= filters_[b].magnitudeAt(sampleRate_, frequency);
        }
    }
    return m;
}

} // namespace vs
