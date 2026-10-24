// vs_bench - offline benchmark and file processor for the VoiceStudio processing chain.
//
//   vs_bench                         Synthetic benchmark of every processing mode (P50/P95/P99)
//   vs_bench --seconds 120           Longer run
//   vs_bench --wav in.wav --out dir  Process a recording with each mode and write WAVs for
//                                    listening tests (original, dsp, rnnoise, gate, combined)
//
// Hardware independent: no audio devices are opened. Timings are per 10 ms frame on this CPU.

#include "WavFile.h"

#include "audio/VoiceProcessingChain.h"
#include "audio/buffers/Resampler.h"
#include "audio/monitoring/StatsCollector.h"
#include "core/DspMath.h"
#include "core/RealtimeThread.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace vs;

namespace {

constexpr std::size_t kFrame = VoiceProcessingChain::kFrameSize;
constexpr int kRate = VoiceProcessingChain::kSampleRate;

struct Mode {
    const char* name;
    void (*configure)(EngineParameters&);
};

void base(EngineParameters& p) {
    p.resetToDefaults();
    p.set(ParamId::CompressorEnabled, 0);
    p.set(ParamId::GateEnabled, 0);
    p.set(ParamId::HighPassEnabled, 0);
}

const Mode kModes[] = {
    {"passthrough", [](EngineParameters& p) { base(p); p.set(ParamId::SuppressionEnabled, 0); }},
    {"gate", [](EngineParameters& p) { base(p); p.set(ParamId::SuppressionEnabled, 0); p.set(ParamId::GateEnabled, 1); }},
    {"dsp", [](EngineParameters& p) { base(p); p.set(ParamId::SuppressionMode, 0); }},
    {"rnnoise", [](EngineParameters& p) { base(p); p.set(ParamId::SuppressionMode, 1); }},
    {"combined", [](EngineParameters& p) {
         p.resetToDefaults();
         p.set(ParamId::SuppressionMode, 1);
         p.set(ParamId::ClarityDb, 2);
         p.set(ParamId::BassDb, 1);
     }},
    {"combined+pitch", [](EngineParameters& p) {
         p.resetToDefaults();
         p.set(ParamId::SuppressionMode, 1);
         p.set(ParamId::PitchEnabled, 1);
         p.set(ParamId::PitchSemitones, 3);
     }},
};

// Synthetic test material: voiced "syllables" (harmonic series with vibrato) alternating with
// pauses, over broadband noise plus 100 Hz hum and keyboard-like clicks. Ground truth for which
// samples contain voice is returned so noise-only attenuation can be measured.
std::vector<float> makeTestSignal(double seconds, std::vector<bool>& voiced) {
    const auto n = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> x(n);
    voiced.assign(n, false);
    std::mt19937 rng(1234);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double cycle = std::fmod(t, 1.2);
        const bool speaking = cycle < 0.7;
        float v = 0.0f;
        if (speaking) {
            const double f0 = 140.0 + 15.0 * std::sin(2 * 3.14159 * 5 * t);
            phase += 2 * 3.14159 * f0 / kRate;
            const double env = std::sin(3.14159 * cycle / 0.7);
            for (int h = 1; h <= 12; ++h) {
                v += static_cast<float>(env * 0.25 / h * std::sin(h * phase));
            }
        }
        voiced[i] = speaking;
        float bg = 0.02f * noise(rng) + 0.01f * static_cast<float>(std::sin(2 * 3.14159 * 100 * t));
        if (uni(rng) < 0.0004f) {
            bg += 0.3f * (uni(rng) - 0.5f); // clicks
        }
        x[i] = v + bg;
    }
    return x;
}

struct RunResult {
    TimingStats timing;
    double noiseReductionDb = 0.0; // energy reduction in noise-only (pause) segments
    double voiceChangeDb = 0.0;    // energy change in voiced segments (closer to 0 = better preserved)
};

RunResult run(const Mode& mode, const std::vector<float>& input, const std::vector<bool>* voiced, std::vector<float>* output) {
    EngineParameters params;
    mode.configure(params);
    VoiceProcessingChain chain;
    FrameDiagnostics diag;
    std::vector<float> timings;
    timings.reserve(input.size() / kFrame);
    if (output) {
        output->assign(input.size(), 0.0f);
    }

    double inNoise = 0, outNoise = 0, inVoice = 0, outVoice = 0;
    float frame[kFrame];
    const int latency = mode.name[0] == 'r' || mode.name[0] == 'c' ? static_cast<int>(kFrame) : 0;
    for (std::size_t off = 0; off + kFrame <= input.size(); off += kFrame) {
        std::copy(input.begin() + static_cast<std::ptrdiff_t>(off), input.begin() + static_cast<std::ptrdiff_t>(off + kFrame), frame);
        const std::int64_t t0 = HighResClock::now();
        chain.process(frame, params, diag);
        timings.push_back(static_cast<float>(HighResClock::ticksToMicroseconds(HighResClock::now() - t0)));
        if (output) {
            std::copy(frame, frame + kFrame, output->begin() + static_cast<std::ptrdiff_t>(off));
        }
        if (voiced && off >= kFrame * 100) { // skip 1 s of adaptation
            for (std::size_t i = 0; i < kFrame; ++i) {
                const std::size_t src = off + i >= static_cast<std::size_t>(latency) ? off + i - latency : 0;
                const double a = input[src] * input[src];
                const double b = static_cast<double>(frame[i]) * frame[i];
                if ((*voiced)[src]) {
                    inVoice += a;
                    outVoice += b;
                } else {
                    inNoise += a;
                    outNoise += b;
                }
            }
        }
    }
    RunResult r;
    r.timing = StatsCollector::compute(timings);
    if (voiced && inNoise > 0 && outNoise > 0) {
        r.noiseReductionDb = 10.0 * std::log10(inNoise / outNoise);
    }
    if (voiced && inVoice > 0 && outVoice > 0) {
        r.voiceChangeDb = 10.0 * std::log10(outVoice / inVoice);
    }
    return r;
}

} // namespace

int main(int argc, char** argv) {
    double seconds = 30.0;
    std::string wavPath;
    std::string outDir = ".";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else if (a == "--wav" && i + 1 < argc) {
            wavPath = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else {
            std::printf("usage: vs_bench [--seconds N] [--wav input.wav --out dir]\n");
            return 1;
        }
    }

    ScopedDenormalSuppression denormals;
    {
        VoiceProcessingChain probe;
        std::printf("RNNoise available: %s\n\n", probe.isAvailable(SuppressorKind::RNNoise) ? "yes" : "NO (DSP fallback)");
    }

    if (!wavPath.empty()) {
        tools::WavData wav;
        std::string error;
        if (!tools::readWav(wavPath, wav, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        std::vector<float> input = wav.mono;
        if (wav.sampleRate != kRate) {
            Resampler rs;
            rs.configure(wav.sampleRate, kRate, 4096);
            std::vector<float> converted;
            std::vector<float> buf(8192);
            for (std::size_t off = 0; off < input.size(); off += 4096) {
                const std::size_t n = std::min<std::size_t>(4096, input.size() - off);
                rs.write(input.data() + off, n);
                const std::size_t got = rs.read(buf.data(), buf.size());
                converted.insert(converted.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got));
            }
            input.swap(converted);
            std::printf("Resampled %u Hz -> %d Hz\n", wav.sampleRate, kRate);
        }
        std::filesystem::create_directories(outDir);
        tools::writeWavFloatMono((std::filesystem::path(outDir) / "original.wav").string(), input, kRate);
        for (const Mode& m : kModes) {
            std::vector<float> out;
            const RunResult r = run(m, input, nullptr, &out);
            const auto path = (std::filesystem::path(outDir) / (std::string(m.name) + ".wav")).string();
            tools::writeWavFloatMono(path, out, kRate);
            std::printf("%-16s P50 %7.1f us  P99 %7.1f us  -> %s\n", m.name, r.timing.p50, r.timing.p99, path.c_str());
        }
        return 0;
    }

    std::vector<bool> voiced;
    const std::vector<float> signal = makeTestSignal(seconds, voiced);
    std::printf("Synthetic benchmark: %.0f s of audio, %zu frames of %zu samples (10 ms)\n\n", seconds,
                signal.size() / kFrame, kFrame);
    std::printf("%-16s %9s %9s %9s %9s %9s %8s %12s %12s\n", "mode", "avg us", "p50 us", "p95 us", "p99 us", "max us",
                "RT load", "noise red.", "voice chg");
    for (const Mode& m : kModes) {
        const RunResult r = run(m, signal, &voiced, nullptr);
        std::printf("%-16s %9.1f %9.1f %9.1f %9.1f %9.1f %7.2f%% %9.1f dB %9.1f dB\n", m.name, r.timing.average, r.timing.p50,
                    r.timing.p95, r.timing.p99, r.timing.max, r.timing.average / 10000.0 * 100.0, r.noiseReductionDb,
                    r.voiceChangeDb);
    }
    std::printf("\nRT load = average processing time as a share of the 10 ms frame period (single core).\n"
                "noise red. = energy reduction during pauses; voice chg = energy change while speaking.\n"
                "The synthetic signal is not speech: use --wav with real recordings for quality judgements.\n");
    return 0;
}
