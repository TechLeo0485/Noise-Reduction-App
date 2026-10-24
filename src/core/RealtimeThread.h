#pragma once

#include <cstdint>

namespace vs {

// RAII: enables flush-to-zero / denormals-are-zero on the current thread so IIR filters
// decaying towards silence never hit the (very slow) denormal floating-point path.
class ScopedDenormalSuppression {
public:
    ScopedDenormalSuppression() noexcept;
    ~ScopedDenormalSuppression();
    ScopedDenormalSuppression(const ScopedDenormalSuppression&) = delete;
    ScopedDenormalSuppression& operator=(const ScopedDenormalSuppression&) = delete;

private:
    unsigned int previousCsr_ = 0;
};

// RAII: registers the current thread with the Multimedia Class Scheduler Service (MMCSS)
// using the "Pro Audio" task, which raises its scheduling priority for audio work.
class ScopedMmcss {
public:
    explicit ScopedMmcss(const wchar_t* taskName = L"Pro Audio") noexcept;
    ~ScopedMmcss();
    ScopedMmcss(const ScopedMmcss&) = delete;
    ScopedMmcss& operator=(const ScopedMmcss&) = delete;

    [[nodiscard]] bool active() const noexcept { return handle_ != nullptr; }

private:
    void* handle_ = nullptr;
};

// Thin wrapper over QueryPerformanceCounter.
class HighResClock {
public:
    [[nodiscard]] static std::int64_t now() noexcept;
    [[nodiscard]] static double ticksToMicroseconds(std::int64_t ticks) noexcept;
    [[nodiscard]] static double ticksToMilliseconds(std::int64_t ticks) noexcept {
        return ticksToMicroseconds(ticks) * 0.001;
    }
};

// Sets a debugger-visible thread name (non-real-time helper).
void setCurrentThreadName(const wchar_t* name) noexcept;

} // namespace vs
