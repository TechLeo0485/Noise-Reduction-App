#pragma once

#include <string>
#include <vector>

namespace vs {

struct LatencyTestOptions {
    std::wstring outputDeviceId; // render endpoint that plays the test pulses
    std::wstring inputDeviceId;  // capture endpoint that hears them
    int pulses = 6;
    double intervalMs = 400.0;
    double toneHz = 1000.0;
    double pulseMs = 6.0;
    double amplitude = 0.5;       // -6 dBFS
    double thresholdAboveFloorDb = 20.0;
};

struct LatencyTestResult {
    bool ok = false;
    std::string error;
    int pulsesSent = 0;
    int pulsesDetected = 0;
    std::vector<double> latenciesMs;
    double medianMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double noiseFloorDb = 0.0;
    double peakDb = 0.0;
    std::string outputName;
    std::string inputName;
    std::string outputFormat;
    std::string inputFormat;
};

// Measures render -> capture latency by playing short tone bursts on one endpoint and
// detecting them on another, using WASAPI's QPC-timestamped stream positions on both sides.
//
// Typical uses:
//   * Virtual cable check: output = "CABLE Input", input = "CABLE Output". Confirms the cable
//     passes audio and measures its own latency.
//   * Device round trip: physical loopback cable from an interface output to its input.
//   * Application end-to-end: see docs/latency-benchmarks.md (subtract the baselines).
//
// Blocking (runs for roughly pulses * interval + 1 s). Must NOT be called on an audio thread.
// The calling thread must have COM initialised (MTA recommended).
[[nodiscard]] LatencyTestResult runLoopbackLatencyTest(const LatencyTestOptions& options);

} // namespace vs
