#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace vs {

#pragma warning(push)
#pragma warning(disable : 4324) // structure padded due to alignment specifier (intentional)

// Lock-free single-producer / single-consumer ring buffer for trivially copyable items.
//
// - All storage is allocated in the constructor. Push/pop never allocate and never block,
//   so both ends may be used from real-time threads.
// - Exactly one thread may call the producer functions and exactly one thread the consumer
//   functions at any time. Ownership of an end may move between threads only across a
//   synchronisation point (e.g. thread join + create).
// - Read/write counters are monotonically increasing 64-bit values; they never wrap in practice.
template <typename T>
class SpscRingBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRingBuffer requires trivially copyable T");

public:
    explicit SpscRingBuffer(std::size_t minCapacity)
        : capacity_(roundUpPow2(std::max<std::size_t>(minCapacity, 2))),
          mask_(capacity_ - 1),
          storage_(std::make_unique<T[]>(capacity_)) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // ---- Producer side -------------------------------------------------------------------

    [[nodiscard]] std::size_t freeSpace() const noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        return capacity_ - (w - r);
    }

    // Writes all `count` items or nothing. Returns false if there is not enough space.
    bool tryPush(const T* data, std::size_t count) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (capacity_ - (w - r) < count) {
            return false;
        }
        copyIn(w, data, count);
        write_.store(w + count, std::memory_order_release);
        return true;
    }

    // ---- Consumer side -------------------------------------------------------------------

    [[nodiscard]] std::size_t available() const noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        return w - r;
    }

    // Reads up to `maxCount` items. Returns the number read.
    std::size_t pop(T* out, std::size_t maxCount) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t n = std::min(maxCount, w - r);
        copyOut(r, out, n);
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    // Discards up to `count` items. Returns the number discarded.
    std::size_t skip(std::size_t count) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t n = std::min(count, w - r);
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    // ---- Either side (approximate) ---------------------------------------------------------

    [[nodiscard]] std::size_t sizeApprox() const noexcept {
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t r = read_.load(std::memory_order_acquire);
        return w >= r ? w - r : 0;
    }

    // Only valid while neither producer nor consumer is active.
    void resetUnsafe() noexcept {
        write_.store(0, std::memory_order_relaxed);
        read_.store(0, std::memory_order_relaxed);
    }

private:
    static std::size_t roundUpPow2(std::size_t v) noexcept {
        std::size_t p = 1;
        while (p < v) {
            p <<= 1;
        }
        return p;
    }

    void copyIn(std::size_t pos, const T* src, std::size_t n) noexcept {
        const std::size_t start = pos & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::memcpy(storage_.get() + start, src, first * sizeof(T));
        if (n > first) {
            std::memcpy(storage_.get(), src + first, (n - first) * sizeof(T));
        }
    }

    void copyOut(std::size_t pos, T* dst, std::size_t n) const noexcept {
        const std::size_t start = pos & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::memcpy(dst, storage_.get() + start, first * sizeof(T));
        if (n > first) {
            std::memcpy(dst + first, storage_.get(), (n - first) * sizeof(T));
        }
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> storage_;

    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};

#pragma warning(pop)

} // namespace vs
