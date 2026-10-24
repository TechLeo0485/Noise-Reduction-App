#pragma once

#include <cstddef>
#include <vector>

namespace vs {

class EngineMetrics;

struct TimingStats {
    double average = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
    std::size_t samples = 0;
};

// Non-real-time consumer of the engine's timing rings. Keeps a sliding window of recent
// samples and computes percentiles on demand (sorting happens here, never on audio threads).
class StatsCollector {
public:
    explicit StatsCollector(std::size_t windowSize = 3000); // 3000 frames = 30 s

    void drain(EngineMetrics& metrics);
    void clear();

    [[nodiscard]] TimingStats frameStats() const { return compute(frameWindow_); }
    [[nodiscard]] TimingStats callbackStats() const { return compute(callbackWindow_); }

    [[nodiscard]] static TimingStats compute(const std::vector<float>& window);

private:
    struct Window {
        std::vector<float> values;
        std::size_t next = 0;
        bool full = false;
    };
    static void append(Window& w, std::size_t capacity, const float* data, std::size_t n);
    [[nodiscard]] static TimingStats compute(const Window& w);

    std::size_t capacity_;
    Window frameWindow_;
    Window callbackWindow_;
    std::vector<float> scratch_;
};

} // namespace vs
