#include "core/RealtimeThread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#include <xmmintrin.h>

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

} // namespace vs
