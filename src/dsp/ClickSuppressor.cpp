#include "dsp/ClickSuppressor.h"

#include "core/Parameters.h"

#include <algorithm>
#include <cmath>

namespace vs {

namespace {
constexpr float kSplitHz = 1500.0f;
constexpr float kOnsetDb = 12.0f;        // jump of the high band above its recent peak that may start a click
constexpr float kBandMarginDb = 9.0f;    // ... at least this much more than the low band's jump
constexpr float kDecayDb = 8.0f;         // ... and faded by this much 4-6 ms later
constexpr float kHoldAboveDb = 6.0f;     // a click lasts while the high band stays this far above the reference
constexpr int kMaxClickBlocks = 40;      // longer is not a click (40 ms)
constexpr float kFloorDb = -65.0f;       // quieter blocks are never clicks (inaudible in a call)
constexpr float kMaxReductionDb = 40.0f; // in a quiet gap nothing above the split is worth keeping
constexpr std::size_t kFifoSize = 8192;  // power of two, far above latency + any call size in the chain

float powerDb(const float* x, int n) noexcept {
    double e = 0.0;
    for (int i = 0; i < n; ++i) {
        e += static_cast<double>(x[i]) * x[i];
    }
    return static_cast<float>(10.0 * std::log10(e / n + 1e-12));
}
} // namespace

void ClickSuppressor::prepare(float sampleRate, std::size_t maxBlockSize) {
    dry_.assign(std::max<std::size_t>(maxBlockSize, kBlock), 0.0f);
    // Linkwitz-Riley crossover (4th order): both bands are real filters, so a click is measured and turned down
    // only above the split, and low + high has a flat magnitude response (an all-pass: only the phase turns).
    const BiquadCoeffs lp = designLowPass(sampleRate, kSplitHz, 0.70710678f);
    const BiquadCoeffs hp = designHighPass(sampleRate, kSplitHz, 0.70710678f);
    low1_.setCoeffs(lp);
    low2_.setCoeffs(lp);
    high1_.setCoeffs(hp);
    high2_.setCoeffs(hp);
    fifo_.assign(kFifoSize, 0.0f);
    reset();
}

void ClickSuppressor::reset() noexcept {
    low1_.reset();
    low2_.reset();
    high1_.reset();
    high2_.reset();
    for (auto& b : low_) {
        b.fill(0.0f);
    }
    for (auto& b : high_) {
        b.fill(0.0f);
    }
    energyDb_.fill(-120.0f);
    lowEnergyDb_.fill(-120.0f);
    gain_.fill(1.0f);
    inputCount_ = 0;
    analysed_ = decided_ = emitted_ = 0;
    clickReferenceDb_ = -120.0f;
    history_.fill(-120.0f);
    lowHistory_.fill(-120.0f);
    clickBlocks_ = 0;
    lastGain_ = 1.0f;
    clicks_ = 0;
    lastReductionDb_ = 0.0f;
    std::fill(fifo_.begin(), fifo_.end(), 0.0f);
    fifoRead_ = 0;
    fifoWrite_ = static_cast<std::size_t>(kLatencySamples);
    delayedOutput_ = enabled_;
}

void ClickSuppressor::updateParameters(const EngineParameters& params) noexcept {
    enabled_ = params.getBool(ParamId::ClickRemovalEnabled);
}

void ClickSuppressor::process(float* samples, std::size_t count) noexcept {
    while (count > 0) {
        const std::size_t n = std::min(count, dry_.size());
        std::copy(samples, samples + n, dry_.begin());
        run(samples, n); // samples now hold the processed, delayed signal
        if (enabled_ != delayedOutput_) {
            // Switched: crossfade from what was playing to the other path over this block.
            for (std::size_t i = 0; i < n; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(n);
                samples[i] = enabled_ ? (1.0f - t) * dry_[i] + t * samples[i] : (1.0f - t) * samples[i] + t * dry_[i];
            }
            delayedOutput_ = enabled_;
        } else if (!enabled_) {
            std::copy(dry_.begin(), dry_.begin() + static_cast<std::ptrdiff_t>(n), samples);
        }
        samples += n;
        count -= n;
    }
}

void ClickSuppressor::run(float* samples, std::size_t count) noexcept {
    float minGain = 1.0f;
    const float before = lastGain_;
    for (std::size_t i = 0; i < count; ++i) {
        const float x = samples[i];
        const float low = low2_.process(low1_.process(x));
        const std::size_t slot = static_cast<std::size_t>(analysed_ % kRing);
        low_[slot][static_cast<std::size_t>(inputCount_)] = low;
        high_[slot][static_cast<std::size_t>(inputCount_)] = high2_.process(high1_.process(x));
        if (++inputCount_ == kBlock) {
            inputCount_ = 0;
            analyseBlock();
        }
        samples[i] = fifo_[fifoRead_];
        fifoRead_ = (fifoRead_ + 1) & (kFifoSize - 1);
    }
    minGain = std::min({minGain, before, lastGain_});
    lastReductionDb_ = 20.0f * std::log10(std::max(minGain, 1e-6f));
}

void ClickSuppressor::analyseBlock() noexcept {
    const std::size_t slot = static_cast<std::size_t>(analysed_ % kRing);
    energyDb_[slot] = powerDb(high_[slot].data(), kBlock);
    lowEnergyDb_[slot] = powerDb(low_[slot].data(), kBlock);
    ++analysed_;
    // A block is decided once its look-ahead has been analysed, and written out once the next one is decided
    // (the gain ramps towards the lower of the two, so it is down before a click's first edge).
    while (decided_ + kLookaheadBlocks < analysed_) {
        decide(decided_);
        ++decided_;
    }
    while (emitted_ + 1 < decided_) {
        float out[kBlock];
        outputBlock(emitted_, out);
        for (float v : out) {
            fifo_[fifoWrite_] = v;
            fifoWrite_ = (fifoWrite_ + 1) & (kFifoSize - 1);
        }
        ++emitted_;
    }
}

void ClickSuppressor::decide(long long block) noexcept {
    const auto at = [this](long long b) { return energyDb_[static_cast<std::size_t>(b % kRing)]; };
    const float e = at(block);
    const float low = lowEnergyDb_[static_cast<std::size_t>(block % kRing)];
    const float reference = *std::max_element(history_.begin(), history_.end());
    const float lowReference = *std::max_element(lowHistory_.begin(), lowHistory_.end());
    float gain = 1.0f;
    const auto reduce = [&] {
        const float db = std::clamp(clickReferenceDb_ - e, -kMaxReductionDb, 0.0f);
        return std::pow(10.0f, db / 20.0f);
    };
    if (clickBlocks_ > 0) {
        if (e > clickReferenceDb_ + kHoldAboveDb && clickBlocks_ < kMaxClickBlocks) {
            ++clickBlocks_;
            gain = reduce();
        } else {
            clickBlocks_ = 0;
        }
    }
    const float jump = e - reference;
    const auto lowAt = [this](long long b) { return lowEnergyDb_[static_cast<std::size_t>(b % kRing)]; };
    // A word starting after a pause: the low band jumps about as much and keeps its energy (the voice goes on).
    // A click's low band, if it jumps at all, dies away as fast as its high band.
    const float lowLater = std::min({lowAt(block + kLookaheadBlocks - 2), lowAt(block + kLookaheadBlocks - 1),
                                     lowAt(block + kLookaheadBlocks)});
    const bool wordOnset = jump - (low - lowReference) < kBandMarginDb && lowLater > low - kDecayDb;
    if (clickBlocks_ == 0 && e > kFloorDb && jump >= kOnsetDb && !wordOnset) {
        const float later = std::min({at(block + kLookaheadBlocks - 2), at(block + kLookaheadBlocks - 1),
                                      at(block + kLookaheadBlocks)});
        if (later <= e - kDecayDb) {
            clickBlocks_ = 1;
            clickReferenceDb_ = reference; // fixed for the whole click, so it cannot raise its own reference
            ++clicks_;
            gain = reduce();
        }
    }
    const auto h = static_cast<std::size_t>(block % kHistory);
    // The real level, also inside a click: storing the click's reference instead locked onto voiced speech (every
    // pulse after a first misjudged one stayed 12 dB above a history of references). A second click within 30 ms of
    // a first can then be missed; a mouse button's release comes 60-140 ms after the press.
    history_[h] = e;
    lowHistory_[h] = low;
    gain_[static_cast<std::size_t>(block % kRing)] = enabled_ ? gain : 1.0f;
}

void ClickSuppressor::outputBlock(long long block, float* out) noexcept {
    const std::size_t slot = static_cast<std::size_t>(block % kRing);
    const float target = std::min(gain_[slot], gain_[static_cast<std::size_t>((block + 1) % kRing)]);
    const float start = lastGain_;
    for (int i = 0; i < kBlock; ++i) {
        const float g = start + (target - start) * static_cast<float>(i + 1) / kBlock;
        out[i] = low_[slot][static_cast<std::size_t>(i)] + g * high_[slot][static_cast<std::size_t>(i)];
    }
    lastGain_ = target;
}

} // namespace vs
