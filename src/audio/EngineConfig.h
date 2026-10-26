#pragma once

#include "audio/wasapi/WasapiCommon.h"

#include <string>

namespace vs {

// Device routing and buffering configuration. Changing any of these requires a stream
// restart (performed by EngineSupervisor off the real-time threads).
// Upper limit for the whole delay from the microphone to the app that records the virtual microphone.
inline constexpr double kMaxTotalLatencyMs = 300.0;
// Delay of the virtual microphone itself (VoiceStudio's driver reads ~20 ms behind; VB-CABLE is similar).
inline constexpr double kVirtualMicLatencyMs = 25.0;

struct EngineConfig {
    std::wstring inputDeviceId;      // empty + inputFollowDefault -> Windows default microphone
    bool inputFollowDefault = true;
    std::wstring outputDeviceId;     // processed output, normally the "CABLE Input" endpoint
    bool outputFollowDefault = false; // off by default: the default output is usually speakers
    std::wstring monitorDeviceId;    // headphones for self-monitoring; empty -> default output
    bool monitorEnabled = false;

    BufferMode bufferMode = BufferMode::Default;
    double safeBufferMs = 30.0;      // device buffer used in BufferMode::Safe
    double outputTargetMs = 12.0;    // requested jitter-buffer fill (raised automatically if unsafe)
    double maxExtraMs = 40.0;        // backlog above target that triggers a latency reset (at most 60 ms)
    double deadlineMs = 5.0;         // per-frame processing budget for deadline-miss accounting
    bool autoRecover = true;

    bool operator==(const EngineConfig&) const = default;
};

} // namespace vs
