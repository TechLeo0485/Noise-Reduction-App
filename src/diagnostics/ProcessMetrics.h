#pragma once

#include <cstdint>

namespace vs {

struct ProcessSample {
    double cpuPercent = -1.0;        // share of total machine CPU capacity; -1 until two samples exist
    double workingSetMb = 0.0;
    double privateBytesMb = 0.0;
};

// Measures this process's CPU and memory usage from Windows counters
// (GetProcessTimes / GetProcessMemoryInfo). Call sample() periodically from one thread.
class ProcessMetrics {
public:
    ProcessSample sample();

private:
    std::uint64_t lastProcessTime_ = 0; // 100 ns units (kernel + user)
    std::uint64_t lastWallTime_ = 0;    // 100 ns units
    bool hasPrevious_ = false;
};

} // namespace vs
