// vs_isolation - measures Strict Voice Isolation (VoxLock INT8 Full) outside the application.
//
//   vs_isolation --enroll me.wav --in mix.wav --out out.wav
//       Real-time run: the full VoiceProcessingChain (isolation + RNNoise + default effects) is fed one
//       10 ms frame every 10 ms of wall-clock time, exactly like the capture thread, while VoxLock runs on
//       the isolator's worker thread. Reports the measured isolation latency, late chunks (silence
//       inserted), inference times and CPU use. Output: 48 kHz float WAV.
//
//   vs_isolation --offline --enroll me.wav --in mix.wav --out out16k.wav [--k 2]
//       Deterministic 16 kHz run of the C++ port of voxlock.core.Stream (no threads, no resampling),
//       for bit-level comparison with the official Python implementation.
//
//   --isolator-only   real-time run of the isolator alone (no suppression or effects)
//   --set key=value   set a chain parameter for the real-time run (keys as in settings.json, e.g.
//                     --set gateUseVad=0 --set gateThresholdDb=-36); repeatable
//   --spin 0|1        ONNX Runtime intra-op thread spinning (offline mode; the app uses 0)
//   --log file        write the application log (e.g. which voice sample was chosen) to `file`
//
// Input WAVs may have any sample rate; the voice sample needs at least 3 s of speech.

#include "WavFile.h"

#include "ai/voxlock/VoxLockEnrollment.h"
#include "ai/voxlock/VoxLockRuntime.h"
#include "audio/VoiceProcessingChain.h"
#include "audio/buffers/Resampler.h"
#include "core/RealtimeThread.h"
#include "diagnostics/Logger.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace vs;

namespace {

std::filesystem::path exeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::vector<float> resample(const std::vector<float>& in, double from, double to) {
    if (from == to) {
        return in;
    }
    constexpr int kHalfTaps = 48;
    Resampler r;
    r.configure(from, to, in.size() + kHalfTaps * 4 + 16, kHalfTaps);
    r.write(in.data(), in.size());
    const std::vector<float> tail(static_cast<std::size_t>(std::ceil(kHalfTaps * from / std::min(from, to))) + 8, 0.0f);
    r.write(tail.data(), tail.size());
    const auto expected = static_cast<std::size_t>(std::llround(static_cast<double>(in.size()) * to / from));
    std::vector<float> out(expected + 64);
    out.resize(std::min(r.read(out.data(), out.size()), expected));
    return out;
}

bool load(const std::string& path, double rate, std::vector<float>& out) {
    tools::WavData wav;
    std::string error;
    if (!tools::readWav(path, wav, error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    out = resample(wav.mono, wav.sampleRate, rate);
    return true;
}

double percentile(std::vector<double> v, double q) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))];
}

double cpuSeconds() {
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    auto toSec = [](FILETIME f) {
        return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 1e-7;
    };
    return toSec(kernel) + toSec(user);
}

// Measures every chunk.onnx call.
class TimingModel final : public voxlock::IChunkModel {
public:
    explicit TimingModel(std::unique_ptr<voxlock::IChunkModel> inner) : inner_(std::move(inner)) {}
    [[nodiscard]] int framesPerRun() const noexcept override { return inner_->framesPerRun(); }
    bool run(const float* wav, const float* hIn, const float* cIn, float* frames, float* hOut, float* cOut) noexcept override {
        const std::int64_t t0 = HighResClock::now();
        const bool ok = inner_->run(wav, hIn, cIn, frames, hOut, cOut);
        times.push_back(HighResClock::ticksToMilliseconds(HighResClock::now() - t0));
        return ok;
    }
    std::vector<double> times;

private:
    std::unique_ptr<voxlock::IChunkModel> inner_;
};

int runOffline(const std::vector<float>& voice16k, const std::vector<float>& input16k, const std::string& outPath, int k,
               bool spin) {
    std::string error;
    const std::filesystem::path dir = exeDirectory();
    auto runtime = voxlock::Runtime::create(dir, dir / L"models" / L"voxlock", error);
    if (!runtime) {
        std::fprintf(stderr, "runtime: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", runtime->description().c_str());
    std::vector<float> sample;
    voxlock::EnrollmentTensors tensors;
    if (!voxlock::fitSample(voice16k, sample, error) || !runtime->enroll(sample, tensors, error)) {
        std::fprintf(stderr, "enroll: %s\n", error.c_str());
        return 1;
    }
    voxlock::ChunkModelOptions options;
    options.framesPerRun = k;
    options.allowSpinning = spin;
    const std::int64_t t0 = HighResClock::now();
    auto inner = runtime->createChunkModel(tensors, options, error);
    if (!inner) {
        std::fprintf(stderr, "chunk model: %s\n", error.c_str());
        return 1;
    }
    std::printf("session created in %.0f ms (K=%d, %d ms chunks)\n", HighResClock::ticksToMilliseconds(HighResClock::now() - t0), k,
                k * 8);
    TimingModel model(std::move(inner));
    voxlock::Stream stream(k, 160);
    std::vector<float> out;
    std::vector<float> block(static_cast<std::size_t>(stream.maxOutputSamples()));
    std::vector<float> padded = input16k;
    padded.resize(input16k.size() + 2048, 0.0f); // run past the end so every input sample has output
    for (std::size_t i = 0; i + 160 <= padded.size(); i += 160) {
        const int n = stream.push(model, padded.data() + i, 160, block.data());
        if (n < 0) {
            std::fprintf(stderr, "inference failed\n");
            return 1;
        }
        out.insert(out.end(), block.begin(), block.begin() + n);
    }
    out.resize(input16k.size());
    tools::writeWavFloatMono(outPath, out, voxlock::kSampleRate);
    const double audioMs = static_cast<double>(model.times.size()) * k * 8.0;
    double total = 0.0;
    for (double t : model.times) {
        total += t;
    }
    std::printf("offline: %zu calls, inference p50 %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms per %d ms; RTF %.2f\n",
                model.times.size(), percentile(model.times, 0.5), percentile(model.times, 0.95), percentile(model.times, 0.99),
                percentile(model.times, 1.0), k * 8, total / audioMs);
    return 0;
}

int runRealtime(const std::vector<float>& voice16k, const std::vector<float>& input48k, const std::string& outPath,
                bool isolatorOnly, const std::vector<std::pair<std::string, float>>& overrides) {
    const std::filesystem::path dir = exeDirectory();
    VoiceProcessingChain chain;
    EngineParameters params;
    params.set(ParamId::IsolationEnabled, 1.0f);
    for (const auto& [key, value] : overrides) {
        if (const ParamSpec* spec = findParam(key)) {
            params.set(spec->id, value);
        } else {
            std::fprintf(stderr, "unknown parameter %s\n", key.c_str());
            return 2;
        }
    }
    VoiceIsolator& isolator = chain.isolator();

    const std::int64_t t0 = HighResClock::now();
    isolator.initialize(dir, dir / L"models" / L"voxlock", {});
    isolator.waitIdle();
    std::printf("load: %.0f ms, state %s: %s\n", HighResClock::ticksToMilliseconds(HighResClock::now() - t0),
                isolationStateName(isolator.state()), isolator.status().message.c_str());
    const std::int64_t t1 = HighResClock::now();
    isolator.enroll(voice16k);
    isolator.waitIdle();
    const IsolationStatus st = isolator.status();
    std::printf("enroll + calibrate: %.0f ms, state %s: %s\n", HighResClock::ticksToMilliseconds(HighResClock::now() - t1),
                isolationStateName(st.state), st.message.c_str());
    if (st.state != IsolationState::Ready) {
        return 1;
    }
    std::printf("%s | K=%d (%d ms chunks), %d threads | calibration p50 %.2f ms, p95 %.2f ms\n", st.backend.c_str(),
                st.framesPerRun, st.framesPerRun * 8, st.threads, st.calibrationP50Ms, st.calibrationP95Ms);

    timeBeginPeriod(1);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ScopedMmcss mmcss(L"Pro Audio"); // like the WASAPI capture thread

    constexpr std::size_t n = VoiceProcessingChain::kFrameSize;
    const std::size_t frames = input48k.size() / n;
    std::vector<float> out(frames * n, 0.0f);
    std::vector<double> processUs;
    std::vector<int> latency;
    std::size_t firstDelivered = SIZE_MAX;
    std::size_t silencedAfterStart = 0;
    const double cpu0 = cpuSeconds();
    const std::int64_t start = HighResClock::now();
    for (std::size_t f = 0; f < frames; ++f) {
        // Wait for the 10 ms tick of this frame (absolute schedule, no drift).
        const double dueMs = static_cast<double>(f) * 10.0;
        const double nowMs = HighResClock::ticksToMilliseconds(HighResClock::now() - start);
        if (dueMs > nowMs) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((dueMs - nowMs) * 10000.0);
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
        }
        float* frame = out.data() + f * n;
        std::memcpy(frame, input48k.data() + f * n, n * sizeof(float));
        const std::int64_t p0 = HighResClock::now();
        bool delivering = false;
        int latencySamples = 0;
        if (isolatorOnly) {
            IsolationFrameInfo info;
            isolator.process(frame, true, info);
            delivering = info.delivering;
            latencySamples = info.latencySamples;
        } else {
            FrameDiagnostics diag;
            chain.process(frame, params, diag);
            delivering = diag.isolationDelivering;
            latencySamples = diag.isolationLatencySamples;
        }
        processUs.push_back(HighResClock::ticksToMicroseconds(HighResClock::now() - p0));
        if (delivering) {
            firstDelivered = std::min(firstDelivered, f);
            latency.push_back(latencySamples);
        } else if (firstDelivered != SIZE_MAX) {
            ++silencedAfterStart;
        }
    }
    const double wallSec = HighResClock::ticksToMilliseconds(HighResClock::now() - start) / 1000.0;
    const double cpu = cpuSeconds() - cpu0;
    CloseHandle(timer);
    timeEndPeriod(1);

    // Inference times recorded by the isolator's worker.
    std::vector<double> inference;
    float us = 0.0f;
    while (isolator.metrics().inferenceUs.pop(&us, 1) == 1) {
        inference.push_back(us / 1000.0);
    }
    std::vector<double> latencyMs;
    for (int l : latency) {
        latencyMs.push_back(l * 1000.0 / VoiceProcessingChain::kSampleRate);
    }
    const VoiceIsolator::Metrics& m = isolator.metrics();
    std::printf("real-time run: %.1f s audio in %.1f s wall clock\n", static_cast<double>(frames) / 100.0, wallSec);
    std::printf("first isolated frame after %.0f ms (start-up silence)\n",
                firstDelivered == SIZE_MAX ? -1.0 : static_cast<double>(firstDelivered) * 10.0);
    std::printf("isolation latency (measured, 48 kHz in -> out): min %.1f ms, median %.1f ms, max %.1f ms\n",
                percentile(latencyMs, 0.0), percentile(latencyMs, 0.5), percentile(latencyMs, 1.0));
    std::printf("late chunks (silence inserted): %llu underruns, %zu silent frames after start; overruns %llu, "
                "inference errors %llu\n",
                static_cast<unsigned long long>(m.underruns.load()), silencedAfterStart,
                static_cast<unsigned long long>(m.overruns.load()), static_cast<unsigned long long>(m.modelFailures.load()));
    std::printf("inference (last %zu chunks): p50 %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms\n", inference.size(),
                percentile(inference, 0.5), percentile(inference, 0.95), percentile(inference, 0.99), percentile(inference, 1.0));
    std::printf("capture-thread cost of %s: p50 %.1f us, p99 %.1f us, max %.1f us per 10 ms frame\n",
                isolatorOnly ? "isolator.process()" : "chain.process()", percentile(processUs, 0.5), percentile(processUs, 0.99),
                percentile(processUs, 1.0));
    std::printf("process CPU: %.0f%% of one core (%.1f%% of the 20 logical processors)\n", 100.0 * cpu / wallSec,
                100.0 * cpu / wallSec / 20.0);
    tools::writeWavFloatMono(outPath, out, VoiceProcessingChain::kSampleRate);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string enrollPath, inPath, outPath;
    bool offline = false;
    bool isolatorOnly = false;
    bool spin = false;
    std::vector<std::pair<std::string, float>> overrides;
    int k = 2;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--enroll") enrollPath = next();
        else if (a == "--in") inPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--offline") offline = true;
        else if (a == "--isolator-only") isolatorOnly = true;
        else if (a == "--k") k = std::max(1, std::atoi(next().c_str()));
        else if (a == "--spin") spin = next() != "0";
        else if (a == "--log") Logger::instance().open(next());
        else if (a == "--set") {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq != std::string::npos) overrides.emplace_back(kv.substr(0, eq), static_cast<float>(std::atof(kv.c_str() + eq + 1)));
        }
    }
    if (enrollPath.empty() || inPath.empty() || outPath.empty()) {
        std::fprintf(stderr, "usage: vs_isolation [--offline [--k K]] [--isolator-only] --enroll me.wav --in mix.wav --out out.wav\n");
        return 2;
    }
    std::vector<float> voice16k;
    std::vector<float> input;
    if (!load(enrollPath, voxlock::kSampleRate, voice16k) ||
        !load(inPath, offline ? voxlock::kSampleRate : VoiceProcessingChain::kSampleRate, input)) {
        return 1;
    }
    return offline ? runOffline(voice16k, input, outPath, k, spin) : runRealtime(voice16k, input, outPath, isolatorOnly, overrides);
}
