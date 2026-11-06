#pragma once

#include "dsp/Biquad.h"
#include "dsp/IAudioProcessor.h"

#include <array>
#include <cstddef>
#include <vector>

namespace vs {

// Removes short clicks and taps - mouse buttons, keys, a pen on the desk - including the ones that land between or on
// words, where the noise suppressor and the gate let them through (the gate is held open while the user talks, and
// VoxLock keeps transients next to the voice it follows).
//
// The signal is split at 1.5 kHz (Linkwitz-Riley: flat in level, only the phase turns around the split) and both
// bands are measured in 1 ms blocks. A block starts a click when
//   * the high band jumps at least 12 dB above its PEAK of the last 30 ms - voiced speech is a train of similar
//     pulses (one per vocal-fold cycle, each a short high-frequency burst), so it never does; a reference built on
//     the quiet moments between pulses took every pulse for a click;
//   * it is not a word starting after a pause: one where the low band jumps nearly as much (within 9 dB) and keeps
//     its energy 4-6 ms later (the voice goes on) - a click's low band, if it jumps at all, dies away with it;
//   * and the high band has fallen at least 8 dB again 4-6 ms later (sustained sounds keep their energy).
// In a click the high band is turned down to the reference (at most 40 dB); the band below 1.5 kHz - the pitch and
// body of the voice - always passes unchanged. 6 ms of look-ahead lets the gain drop before the click's first edge.
// Measured on the user's speech with synthetic mouse clicks and typing (full chain, isolation off, repeatable runs):
// clicks between words that stay audible 38 % -> 23 %, worst +27.7 -> +18.9 dB. The cost: energy rules cannot fully
// tell a click from a consonant or a vocal-fold pulse - on 23 s of unseen speech 8 % of 10 ms frames lost > 1 dB above
// 1.5 kHz (2.9 % > 3 dB). Hence experimental and off by default; a trained model (the Lightweight filter is trained
// with clicks, or a stronger noise suppressor) is the real answer.
//
// On, the output is delayed by kLatencySamples (8 ms); off, the input passes unchanged with no delay. The analysis
// runs either way, and switching crossfades between the two over one process() call (like the chain's switch between
// noise suppressors), so it never clicks or drops out. 48 kHz; any block size.
class ClickSuppressor final : public IAudioProcessor {
public:
    static constexpr int kBlock = 48;                  // 1 ms at 48 kHz
    static constexpr int kLookaheadBlocks = 6;
    // Look-ahead, plus one block for the gain ramp, plus the block being filled: 8 ms.
    static constexpr int kLatencySamples = (kLookaheadBlocks + 2) * kBlock;

    [[nodiscard]] const char* name() const noexcept override { return "Click remover"; }
    void prepare(float sampleRate, std::size_t maxBlockSize) override;
    void reset() noexcept override;
    void updateParameters(const EngineParameters& params) noexcept override;
    void process(float* samples, std::size_t count) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return delayedOutput_ ? kLatencySamples : 0; }

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    // Clicks found since reset() (for tests and diagnostics).
    [[nodiscard]] std::size_t clicksDetected() const noexcept { return clicks_; }
    // Lowest high-band gain applied in the last processed call (dB, 0 = untouched).
    [[nodiscard]] float lastReductionDb() const noexcept { return lastReductionDb_; }

private:
    static constexpr int kRing = kLookaheadBlocks + 3; // blocks of history kept for the delayed output
    static constexpr int kHistory = 30;                // blocks of recent level for the reference (30 ms)
    void analyseBlock() noexcept;
    void decide(long long block) noexcept;
    void outputBlock(long long block, float* out) noexcept;

    void run(float* samples, std::size_t count) noexcept;

    bool enabled_ = true;
    bool delayedOutput_ = true; // what the output currently is: the processed (delayed) path or the dry input
    std::vector<float> dry_;
    Biquad low1_;
    Biquad low2_;
    Biquad high1_;
    Biquad high2_;
    // Ring of the last kRing blocks: low band, high band, high-band energy (dB) and the decided gain.
    std::array<std::array<float, kBlock>, kRing> low_{};
    std::array<std::array<float, kBlock>, kRing> high_{};
    std::array<float, kRing> energyDb_{};
    std::array<float, kRing> lowEnergyDb_{};
    std::array<float, kRing> gain_{};
    std::array<float, kBlock> input_{};
    int inputCount_ = 0;
    long long analysed_ = 0;   // blocks analysed so far
    long long decided_ = 0;    // blocks whose gain is known
    long long emitted_ = 0;    // blocks written out
    float clickReferenceDb_ = -120.0f; // the reference, fixed while inside a click
    std::array<float, kHistory> history_{};    // high band of the last kHistory blocks
    std::array<float, kHistory> lowHistory_{}; // low band of the last kHistory blocks
    int clickBlocks_ = 0;      // > 0 while inside a click
    float lastGain_ = 1.0f;    // gain at the end of the previous output block
    // Output FIFO: starts with kLatencySamples of silence, then the processed blocks.
    std::vector<float> fifo_;
    std::size_t fifoRead_ = 0;
    std::size_t fifoWrite_ = 0;
    std::size_t clicks_ = 0;
    float lastReductionDb_ = 0.0f;
};

} // namespace vs
