#include "diagnostics/ProcessMetrics.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace vs {

namespace {
std::uint64_t toU64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
} // namespace

ProcessSample ProcessMetrics::sample() {
    ProcessSample s;

    FILETIME creation{}, exitTime{}, kernel{}, user{};
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        const std::uint64_t processTime = toU64(kernel) + toU64(user);
        const std::uint64_t wallTime = toU64(now);
        if (hasPrevious_ && wallTime > lastWallTime_) {
            const double cpuDelta = static_cast<double>(processTime - lastProcessTime_);
            const double wallDelta = static_cast<double>(wallTime - lastWallTime_);
            const double cores = static_cast<double>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            s.cpuPercent = cores > 0 ? 100.0 * cpuDelta / (wallDelta * cores) : -1.0;
        }
        lastProcessTime_ = processTime;
        lastWallTime_ = wallTime;
        hasPrevious_ = true;
    }

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        s.workingSetMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        s.privateBytesMb = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    }
    return s;
}

} // namespace vs
