// vs_soak - long-running real-hardware soak test of the full audio engine (no UI).
//
//   vs_soak --hours 8 --out <render index> [--in <capture index>] [--csv soak.csv]
//           [--restart-every <seconds>] [--mode rnnoise|dsp] [--buffer low|default|safe]
//
// Runs EngineSupervisor exactly as the app does and writes one CSV row per minute with
// timings, dropouts, latency, drift, CPU and memory. At the end prints a pass/fail summary.
// See docs/latency-benchmarks.md, "8-hour soak test".

#include "audio/EngineSupervisor.h"
#include "audio/monitoring/StatsCollector.h"
#include "devices/DeviceManager.h"
#include "diagnostics/Logger.h"
#include "diagnostics/ProcessMetrics.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace vs;
using namespace std::chrono;

int main(int argc, char** argv) {
    ComInitializer com(COINIT_MULTITHREADED);
    double hours = 8.0;
    int outIndex = -1;
    int inIndex = -1;
    int restartEverySeconds = 0;
    std::string csvPath = "soak.csv";
    std::string mode = "rnnoise";
    std::string buffer = "default";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--hours" && i + 1 < argc) hours = std::atof(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outIndex = std::atoi(argv[++i]);
        else if (a == "--in" && i + 1 < argc) inIndex = std::atoi(argv[++i]);
        else if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
        else if (a == "--restart-every" && i + 1 < argc) restartEverySeconds = std::atoi(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--buffer" && i + 1 < argc) buffer = argv[++i];
    }

    DeviceManager devices;
    const auto outputs = devices.enumerate(DeviceFlow::Render);
    const auto inputs = devices.enumerate(DeviceFlow::Capture);
    if (outIndex < 0 || outIndex >= static_cast<int>(outputs.size())) {
        std::printf("Render endpoints (--out):\n");
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            std::printf("  [%zu] %s\n", i, toUtf8(outputs[i].name).c_str());
        }
        std::printf("Capture endpoints (--in, default = Windows default):\n");
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            std::printf("  [%zu] %s\n", i, toUtf8(inputs[i].name).c_str());
        }
        std::printf("\nusage: vs_soak --hours 8 --out <index> [--in <index>] [--csv file] [--restart-every s]\n");
        return 1;
    }

    Logger::instance().open(appDataDirectory() / L"logs" / L"vs_soak.log");

    EngineParameters params;
    params.set(ParamId::SuppressionMode, mode == "dsp" ? 0.0f : 1.0f);
    EngineConfig config;
    config.outputDeviceId = outputs[static_cast<std::size_t>(outIndex)].id;
    if (inIndex >= 0 && inIndex < static_cast<int>(inputs.size())) {
        config.inputDeviceId = inputs[static_cast<std::size_t>(inIndex)].id;
        config.inputFollowDefault = false;
    }
    config.bufferMode = buffer == "low" ? BufferMode::LowLatency : buffer == "safe" ? BufferMode::Safe : BufferMode::Default;

    EngineSupervisor supervisor(params);
    supervisor.applyConfig(config);
    supervisor.launch();
    supervisor.setEnabled(true);

    std::FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (csv == nullptr) {
        std::fprintf(stderr, "Cannot write %s\n", csvPath.c_str());
        return 1;
    }
    std::fprintf(csv, "minute,state,frames,deadline_misses,capture_glitches,underruns,overruns,latency_resets,"
                      "p50_us,p95_us,p99_us,max_us,buffered_ms,render_queue_ms,drift_ppm,cpu_percent,working_set_mb,"
                      "private_mb,engine_starts\n");

    StatsCollector stats(6000); // 60 s window
    ProcessMetrics process;
    const auto start = steady_clock::now();
    const auto end = start + duration_cast<steady_clock::duration>(duration<double, std::ratio<3600>>(hours));
    auto nextRow = start + minutes(1);
    auto nextRestart = restartEverySeconds > 0 ? start + seconds(restartEverySeconds) : steady_clock::time_point::max();
    double firstPrivateMb = -1.0;
    double maxPrivateMb = 0.0;
    int minute = 0;

    std::printf("Soak test running for %.2f h -> %s (Ctrl+C to abort)\n", hours, csvPath.c_str());
    while (steady_clock::now() < end) {
        std::this_thread::sleep_for(milliseconds(250));
        EngineMetrics& m = supervisor.engine().metrics();
        stats.drain(m);
        if (steady_clock::now() >= nextRestart) {
            supervisor.requestRestart();
            nextRestart += seconds(restartEverySeconds);
        }
        if (steady_clock::now() < nextRow) {
            continue;
        }
        nextRow += minutes(1);
        ++minute;
        const TimingStats t = stats.frameStats();
        const ProcessSample p = process.sample();
        const EngineStatus st = supervisor.status();
        if (firstPrivateMb < 0 && minute >= 2) {
            firstPrivateMb = p.privateBytesMb; // baseline after warm-up
        }
        maxPrivateMb = std::max(maxPrivateMb, p.privateBytesMb);
        std::fprintf(csv, "%d,%s,%llu,%llu,%llu,%llu,%llu,%llu,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.1f,%.2f,%.1f,%.1f,%llu\n",
                     minute, engineStateName(st.state), static_cast<unsigned long long>(m.framesProcessed.load()),
                     static_cast<unsigned long long>(m.deadlineMisses.load()),
                     static_cast<unsigned long long>(m.captureDiscontinuities.load()),
                     static_cast<unsigned long long>(m.output.underruns.load()),
                     static_cast<unsigned long long>(m.outputOverruns.load()),
                     static_cast<unsigned long long>(m.output.latencyResets.load()), t.p50, t.p95, t.p99, t.max,
                     m.output.bufferedMs.load(), m.output.devicePaddingMs.load(), m.output.driftCorrectionPpm.load(),
                     p.cpuPercent, p.workingSetMb, p.privateBytesMb, static_cast<unsigned long long>(st.restartCount));
        std::fflush(csv);
        std::printf("[%4d min] %-10s underruns %llu  p99 %.0f us  buffered %.1f ms  drift %.0f ppm  cpu %.1f%%  mem %.1f MB\n",
                    minute, engineStateName(st.state), static_cast<unsigned long long>(m.output.underruns.load()), t.p99,
                    m.output.bufferedMs.load(), m.output.driftCorrectionPpm.load(), p.cpuPercent, p.privateBytesMb);
    }
    std::fclose(csv);

    EngineMetrics& m = supervisor.engine().metrics();
    const auto underruns = m.output.underruns.load();
    const auto resets = m.output.latencyResets.load();
    supervisor.shutdown();

    const double memoryGrowth = firstPrivateMb >= 0 ? maxPrivateMb - firstPrivateMb : 0.0;
    std::printf("\nSummary: underruns %llu, latency resets %llu, memory growth %.1f MB\n",
                static_cast<unsigned long long>(underruns), static_cast<unsigned long long>(resets), memoryGrowth);
    // Counters are reset on every engine restart, so with --restart-every these totals only
    // cover the last run; inspect the CSV for the full history.
    const bool pass = memoryGrowth < 5.0 && (restartEverySeconds > 0 || underruns == 0);
    std::printf("%s\n", pass ? "PASS" : "CHECK CSV: dropouts or memory growth detected");
    return pass ? 0 : 2;
}
