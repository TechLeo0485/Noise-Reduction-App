#include "core/RealtimeThread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#include <xmmintrin.h>

#include <algorithm>
#include <vector>

namespace vs {

namespace {
constexpr unsigned int kFtzDazMask = 0x8040; // FTZ (bit 15) | DAZ (bit 6)

double queryFrequency() noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}
} // namespace

ScopedDenormalSuppression::ScopedDenormalSuppression() noexcept : previousCsr_(_mm_getcsr()) {
    _mm_setcsr(previousCsr_ | kFtzDazMask);
}

ScopedDenormalSuppression::~ScopedDenormalSuppression() {
    _mm_setcsr(previousCsr_);
}

ScopedMmcss::ScopedMmcss(const wchar_t* taskName) noexcept {
    DWORD taskIndex = 0;
    handle_ = AvSetMmThreadCharacteristicsW(taskName, &taskIndex);
    if (handle_ != nullptr) {
        AvSetMmThreadPriority(handle_, AVRT_PRIORITY_HIGH);
    }
}

ScopedMmcss::~ScopedMmcss() {
    if (handle_ != nullptr) {
        AvRevertMmThreadCharacteristics(handle_);
    }
}

std::int64_t HighResClock::now() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double HighResClock::ticksToMicroseconds(std::int64_t ticks) noexcept {
    static const double frequency = queryFrequency();
    return static_cast<double>(ticks) * 1.0e6 / frequency;
}

void setCurrentThreadName(const wchar_t* name) noexcept {
    SetThreadDescription(GetCurrentThread(), name);
}

int preferPerformanceCores() noexcept {
    THREAD_POWER_THROTTLING_STATE throttling{};
    throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0; // never EcoQoS
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof(throttling));

    ULONG length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0) {
        return 0;
    }
    std::vector<unsigned char> buffer(length);
    auto* info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data());
    if (!GetSystemCpuSetInformation(info, length, &length, GetCurrentProcess(), 0)) {
        return 0;
    }
    BYTE best = 0;
    BYTE worst = 0xFF;
    std::vector<ULONG> ids;
    for (int pass = 0; pass < 2; ++pass) {
        for (ULONG offset = 0; offset < length;) {
            const auto* set = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
            if (set->Type == CpuSetInformation) {
                const BYTE cls = set->CpuSet.EfficiencyClass;
                if (pass == 0) {
                    best = std::max(best, cls);
                    worst = std::min(worst, cls);
                } else if (cls == best) {
                    ids.push_back(set->CpuSet.Id);
                }
            }
            offset += set->Size;
        }
    }
    if (best == worst || ids.empty()) {
        return 0; // one core class: let the scheduler decide
    }
    return SetThreadSelectedCpuSets(GetCurrentThread(), ids.data(), static_cast<ULONG>(ids.size()))
               ? static_cast<int>(ids.size())
               : 0;
}

} // namespace vs
