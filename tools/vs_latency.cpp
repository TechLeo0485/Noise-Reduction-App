// vs_latency - loopback latency measurement between any render and capture endpoint.
//
//   vs_latency --list
//   vs_latency --out <render index> --in <capture index> [--pulses N]
//
// See docs/latency-benchmarks.md for the measurement procedures (virtual cable, hardware
// loopback, and application end-to-end with baseline subtraction).

#include "audio/wasapi/WasapiCommon.h"
#include "devices/DeviceManager.h"
#include "diagnostics/LoopbackLatencyTester.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vs;

int main(int argc, char** argv) {
    ComInitializer com(COINIT_MULTITHREADED);
    DeviceManager devices;
    if (!devices.isValid()) {
        std::fprintf(stderr, "Cannot access Windows audio devices.\n");
        return 1;
    }
    const auto outputs = devices.enumerate(DeviceFlow::Render);
    const auto inputs = devices.enumerate(DeviceFlow::Capture);

    int outIndex = -1;
    int inIndex = -1;
    int pulses = 8;
    bool list = argc == 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") {
            list = true;
        } else if (a == "--out" && i + 1 < argc) {
            outIndex = std::atoi(argv[++i]);
        } else if (a == "--in" && i + 1 < argc) {
            inIndex = std::atoi(argv[++i]);
        } else if (a == "--pulses" && i + 1 < argc) {
            pulses = std::atoi(argv[++i]);
        }
    }

    if (list || outIndex < 0 || inIndex < 0) {
        std::printf("Render endpoints (--out):\n");
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            std::printf("  [%zu] %s%s  (%s)\n", i, toUtf8(outputs[i].name).c_str(), outputs[i].isDefault ? " [default]" : "",
                        outputs[i].mixFormat.describe().c_str());
        }
        std::printf("Capture endpoints (--in):\n");
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            std::printf("  [%zu] %s%s  (%s)\n", i, toUtf8(inputs[i].name).c_str(), inputs[i].isDefault ? " [default]" : "",
                        inputs[i].mixFormat.describe().c_str());
        }
        std::printf("\nusage: vs_latency --out <index> --in <index> [--pulses N]\n");
        return list ? 0 : 1;
    }
    if (outIndex >= static_cast<int>(outputs.size()) || inIndex >= static_cast<int>(inputs.size())) {
        std::fprintf(stderr, "Device index out of range.\n");
        return 1;
    }

    LatencyTestOptions options;
    options.outputDeviceId = outputs[static_cast<std::size_t>(outIndex)].id;
    options.inputDeviceId = inputs[static_cast<std::size_t>(inIndex)].id;
    options.pulses = pulses;

    std::printf("Playing %d pulses on \"%s\", listening on \"%s\"...\n", pulses,
                toUtf8(outputs[static_cast<std::size_t>(outIndex)].name).c_str(),
                toUtf8(inputs[static_cast<std::size_t>(inIndex)].name).c_str());
    const LatencyTestResult r = runLoopbackLatencyTest(options);

    std::printf("Output format: %s\nInput format:  %s\n", r.outputFormat.c_str(), r.inputFormat.c_str());
    std::printf("Noise floor %.1f dBFS, captured peak %.1f dBFS\n", r.noiseFloorDb, r.peakDb);
    for (std::size_t i = 0; i < r.latenciesMs.size(); ++i) {
        std::printf("  pulse %zu: %.2f ms\n", i + 1, r.latenciesMs[i]);
    }
    if (!r.error.empty()) {
        std::printf("Result: %s\n", r.error.c_str());
    }
    if (r.pulsesDetected > 0) {
        std::printf("Detected %d/%d. Median %.2f ms (min %.2f, max %.2f)\n", r.pulsesDetected, r.pulsesSent, r.medianMs,
                    r.minMs, r.maxMs);
    }
    return r.ok ? 0 : 2;
}
