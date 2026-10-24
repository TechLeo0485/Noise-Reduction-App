#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace vs {

// Plays a mono 48 kHz buffer on a render endpoint (empty ID = Windows default output),
// resampling to the device rate. Blocking; runs on a NON-real-time thread with COM initialised.
// Used by the "voice check" feature so users can hear exactly what the processed output
// sounds like. Returns false and sets `error` on failure; returns early if `cancel` is set.
bool playMonoBuffer(const std::wstring& deviceId, const std::vector<float>& samples48k, std::atomic<bool>& cancel,
                    std::string& deviceName, std::string& error);

} // namespace vs
