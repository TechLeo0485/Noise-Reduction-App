#include "ai/voxlock/SpeakerCheck.h"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <complex>

namespace vs::voxlock {

namespace {

constexpr int kSampleRate = 16000;
constexpr int kPadded = 512;               // FFT size (frame padded to a power of two)
constexpr int kSpectrumBins = kPadded / 2; // Kaldi uses bins 0 .. N/2-1 (no Nyquist)
constexpr double kPreemphasis = 0.97;
constexpr double kLowHz = 20.0;
constexpr double kHighHz = kSampleRate / 2.0;
constexpr int kReferencePieces = 40;       // embeddings averaged for the reference (bounded time)
constexpr std::size_t kReferenceSeconds = 3 * kSampleRate / 2;

double melScale(double hz) {
    return 1127.0 * std::log(1.0 + hz / 700.0);
}

struct FbankTables {
    std::vector<float> window; // povey: hann(N-1)^0.85
    std::vector<float> banks;  // kFbankBins x kSpectrumBins
    FbankTables() : window(kFbankFrame), banks(static_cast<std::size_t>(kFbankBins) * kSpectrumBins, 0.0f) {
        constexpr double kTwoPi = 6.283185307179586476925286766559;
        for (int i = 0; i < kFbankFrame; ++i) {
            window[static_cast<std::size_t>(i)] =
                static_cast<float>(std::pow(0.5 - 0.5 * std::cos(kTwoPi * i / (kFbankFrame - 1)), 0.85));
        }
        const double lo = melScale(kLowHz);
        const double hi = melScale(kHighHz);
        const double step = (hi - lo) / (kFbankBins + 1);
        for (int b = 0; b < kFbankBins; ++b) {
            const double left = lo + b * step;
            const double centre = left + step;
            const double right = centre + step;
            for (int k = 0; k < kSpectrumBins; ++k) {
                const double m = melScale(static_cast<double>(k) * kSampleRate / kPadded);
                const double w = std::max(0.0, std::min((m - left) / (centre - left), (right - m) / (right - centre)));
                banks[static_cast<std::size_t>(b) * kSpectrumBins + static_cast<std::size_t>(k)] = static_cast<float>(w);
            }
        }
    }
};

const FbankTables& tables() {
    static const FbankTables t;
    return t;
}

// Speech of a recording with the pauses cut out: 10 ms blocks within 30 dB of the loud passages.
std::vector<float> speechOnly(const std::vector<float>& audio) {
    constexpr std::size_t kBlock = 160;
    const std::size_t blocks = audio.size() / kBlock;
    std::vector<float> energyDb(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        double e = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i) {
            const double v = audio[b * kBlock + i];
            e += v * v;
        }
        energyDb[b] = static_cast<float>(10.0 * std::log10(e / kBlock + 1e-12));
    }
    std::vector<float> sorted = energyDb;
    if (sorted.empty()) {
        return {};
    }
    const std::size_t p95 = sorted.size() * 95 / 100;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(p95), sorted.end());
    const float threshold = std::max(sorted[p95] - 30.0f, -50.0f);
    std::vector<float> out;
    out.reserve(audio.size());
    for (std::size_t b = 0; b < blocks; ++b) {
        if (energyDb[b] > threshold) {
            out.insert(out.end(), audio.begin() + static_cast<std::ptrdiff_t>(b * kBlock),
                       audio.begin() + static_cast<std::ptrdiff_t>((b + 1) * kBlock));
        }
    }
    return out;
}

} // namespace

int computeFbank(const float* audio16k, std::size_t count, std::vector<float>& out) {
    if (count < static_cast<std::size_t>(kFbankFrame)) {
        out.clear();
        return 0;
    }
    const FbankTables& t = tables();
    const int frames = 1 + static_cast<int>((count - kFbankFrame) / kFbankShift);
    out.assign(static_cast<std::size_t>(frames) * kFbankBins, 0.0f);
    signalsmith::linear::RealFFT<float> fft(kPadded);
    std::vector<float> time(kPadded, 0.0f);
    std::vector<std::complex<float>> freq(kPadded / 2);
    std::vector<float> power(kSpectrumBins);
    std::vector<double> frame(kFbankFrame);
    for (int f = 0; f < frames; ++f) {
        const float* x = audio16k + static_cast<std::size_t>(f) * kFbankShift;
        double mean = 0.0;
        for (int i = 0; i < kFbankFrame; ++i) {
            mean += x[i];
        }
        mean /= kFbankFrame;
        for (int i = 0; i < kFbankFrame; ++i) {
            frame[static_cast<std::size_t>(i)] = x[i] - mean;
        }
        for (int i = kFbankFrame - 1; i > 0; --i) {
            frame[static_cast<std::size_t>(i)] -= kPreemphasis * frame[static_cast<std::size_t>(i - 1)];
        }
        frame[0] -= kPreemphasis * frame[0];
        for (int i = 0; i < kFbankFrame; ++i) {
            time[static_cast<std::size_t>(i)] = static_cast<float>(frame[static_cast<std::size_t>(i)]) * t.window[static_cast<std::size_t>(i)];
        }
        std::fill(time.begin() + kFbankFrame, time.end(), 0.0f);
        fft.fft(time.data(), freq.data());
        power[0] = freq[0].real() * freq[0].real(); // packed: bin 0 holds DC (real) and Nyquist (imag)
        for (int k = 1; k < kSpectrumBins; ++k) {
            power[static_cast<std::size_t>(k)] = std::norm(freq[static_cast<std::size_t>(k)]);
        }
        float* row = out.data() + static_cast<std::size_t>(f) * kFbankBins;
        for (int b = 0; b < kFbankBins; ++b) {
            const float* bank = t.banks.data() + static_cast<std::size_t>(b) * kSpectrumBins;
            double e = 0.0;
            for (int k = 0; k < kSpectrumBins; ++k) {
                e += static_cast<double>(bank[k]) * power[static_cast<std::size_t>(k)];
            }
            row[b] = static_cast<float>(std::log(std::max(e, static_cast<double>(FLT_EPSILON))));
        }
    }
    // Mean normalisation over time (what the speaker models were trained with).
    for (int b = 0; b < kFbankBins; ++b) {
        double mean = 0.0;
        for (int f = 0; f < frames; ++f) {
            mean += out[static_cast<std::size_t>(f) * kFbankBins + static_cast<std::size_t>(b)];
        }
        mean /= frames;
        for (int f = 0; f < frames; ++f) {
            out[static_cast<std::size_t>(f) * kFbankBins + static_cast<std::size_t>(b)] -= static_cast<float>(mean);
        }
    }
    return frames;
}

float similarity(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size() || a.empty()) {
        return 0.0f;
    }
    double dot = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
    }
    return static_cast<float>(dot);
}

std::vector<float> referenceEmbedding(ISpeakerModel& model, const std::vector<float>& recording16k) {
    const std::vector<float> speech = speechOnly(recording16k);
    const std::size_t pieces = speech.size() / kReferenceSeconds;
    if (pieces == 0) {
        return {};
    }
    // Spread over the whole recording, so every speaking style in it counts.
    const std::size_t used = std::min<std::size_t>(pieces, kReferencePieces);
    std::vector<double> sum;
    std::vector<float> e;
    int n = 0;
    for (std::size_t i = 0; i < used; ++i) {
        const std::size_t piece = i * pieces / used;
        if (!model.embed(speech.data() + piece * kReferenceSeconds, kReferenceSeconds, e)) {
            continue;
        }
        sum.resize(e.size(), 0.0);
        for (std::size_t d = 0; d < e.size() && d < sum.size(); ++d) {
            sum[d] += e[d];
        }
        ++n;
    }
    if (n == 0) {
        return {};
    }
    double norm = 0.0;
    for (double v : sum) {
        norm += v * v;
    }
    norm = std::sqrt(norm);
    std::vector<float> out(sum.size());
    for (std::size_t d = 0; d < sum.size(); ++d) {
        out[d] = norm > 0.0 ? static_cast<float>(sum[d] / norm) : 0.0f;
    }
    return out;
}

// ---- VoiceCheck -----------------------------------------------------------------------------------------

VoiceCheck::VoiceCheck() : VoiceCheck(Settings{}) {}

VoiceCheck::VoiceCheck(const Settings& settings) : s_(settings) {
    const auto capacity = static_cast<std::size_t>(std::ceil(std::max(s_.closeSeconds, s_.openSeconds) * kSampleRate / kBlock));
    blocks_.resize(capacity);
    partial_.reserve(kBlock);
    window_.reserve(capacity * kBlock);
    reset();
}

void VoiceCheck::reset() noexcept {
    state_ = State::Open;
    resetPending_ = false;
    silent_ = 0;
    lastScore_ = 1.0f;
    peakDb_ = -60.0f;
    time_ = 0;
    nextCheck_ = static_cast<long long>(s_.checkEvery * kSampleRate);
    lastReset_ = 0;
    newSpeech_ = 0;
    partial_.clear();
    head_ = 0;
    count_ = 0;
    window_.clear();
}

VoiceCheck::Action VoiceCheck::feed(const float* out16k, std::size_t count) {
    Action action = Action::None;
    for (std::size_t i = 0; i < count; ++i) {
        partial_.push_back(out16k[i]);
        if (partial_.size() < static_cast<std::size_t>(kBlock)) {
            continue;
        }
        double e = 0.0;
        for (float v : partial_) {
            e += static_cast<double>(v) * v;
        }
        const auto energyDb = static_cast<float>(10.0 * std::log10(e / kBlock + 1e-12));
        peakDb_ = std::max(energyDb, peakDb_ - 0.01f); // the loudness reference falls by 1 dB per second
        if (energyDb > std::max(-50.0f, peakDb_ - 30.0f)) {
            Block& b = blocks_[head_];
            b.time = time_;
            std::copy(partial_.begin(), partial_.end(), b.samples);
            head_ = (head_ + 1) % blocks_.size();
            count_ = std::min(count_ + 1, blocks_.size());
            newSpeech_ += kBlock;
            silent_ = 0;
            if (state_ == State::Armed) {
                state_ = State::Trial;
            }
        } else {
            silent_ += kBlock;
            if (state_ == State::Muted && silent_ >= static_cast<long long>(s_.pauseSeconds * kSampleRate)) {
                // The other voice stopped. Whoever speaks next passes at once, judged on their own speech only,
                // and starts from a fresh model state.
                state_ = State::Armed;
                count_ = 0;
                newSpeech_ = 0;
                resetPending_ = true;
            }
        }
        partial_.clear();
        time_ += kBlock;

        const bool trial = state_ == State::Trial;
        const auto need = static_cast<std::size_t>(
            (trial ? s_.trialSeconds : state_ == State::Open ? s_.closeSeconds : s_.openSeconds) * kSampleRate / kBlock);
        if (trial) {
            // Judged as soon as there is enough: the other voice passes meanwhile.
            if (newSpeech_ < static_cast<long long>(need) * kBlock) {
                continue;
            }
        } else {
            if (time_ < nextCheck_) {
                continue;
            }
            nextCheck_ = time_ + static_cast<long long>(s_.checkEvery * kSampleRate);
            if (newSpeech_ < static_cast<long long>(s_.minNewSpeech * kSampleRate)) {
                continue;
            }
        }
        newSpeech_ = 0;
        const auto maxAge = static_cast<long long>(s_.maxAge * kSampleRate);
        std::size_t recent = 0;
        while (recent < count_ && recent < need) {
            const Block& b = blocks_[(head_ + blocks_.size() - 1 - recent) % blocks_.size()];
            if (time_ - b.time > maxAge) {
                break;
            }
            ++recent;
        }
        if (recent < need) {
            continue;
        }
        window_.clear();
        for (std::size_t k = recent; k > 0; --k) {
            const Block& b = blocks_[(head_ + blocks_.size() - k) % blocks_.size()];
            window_.insert(window_.end(), b.samples, b.samples + kBlock);
        }
        action = Action::Check;
    }
    return action;
}

bool VoiceCheck::decide(float score) {
    lastScore_ = score;
    if (open() && score < s_.closeBelow) {
        state_ = State::Muted;
        lastReset_ = time_;
        return true;
    }
    if (state_ == State::Trial || (state_ == State::Muted && score > s_.openAbove)) {
        state_ = State::Open;
    }
    return false;
}

bool VoiceCheck::resetDue() noexcept {
    if (resetPending_) {
        resetPending_ = false;
        lastReset_ = time_;
        return true;
    }
    if (state_ == State::Muted && time_ - lastReset_ >= static_cast<long long>(s_.resetEvery * kSampleRate)) {
        lastReset_ = time_;
        return true;
    }
    return false;
}

VoiceCheck::Gate VoiceCheck::gate() const noexcept {
    switch (state_) {
    case State::Muted:
        return Gate::Muted;
    case State::Armed:
        return Gate::OpenOnSpeech;
    default:
        return Gate::Open;
    }
}

float VoiceCheck::speechLevel() const noexcept {
    return static_cast<float>(std::pow(10.0, std::max(-50.0f, peakDb_ - 30.0f) / 10.0));
}

} // namespace vs::voxlock
