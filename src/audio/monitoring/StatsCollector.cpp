#include "audio/monitoring/StatsCollector.h"

#include "audio/monitoring/EngineMetrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vs {

StatsCollector::StatsCollector(std::size_t windowSize) : capacity_(std::max<std::size_t>(windowSize, 16)), scratch_(1024) {
    frameWindow_.values.reserve(capacity_);
    callbackWindow_.values.reserve(capacity_);
}

void StatsCollector::clear() {
    frameWindow_ = Window{};
    callbackWindow_ = Window{};
    frameWindow_.values.reserve(capacity_);
    callbackWindow_.values.reserve(capacity_);
}

void StatsCollector::append(Window& w, std::size_t capacity, const float* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (w.values.size() < capacity) {
            w.values.push_back(data[i]);
        } else {
            w.values[w.next] = data[i];
            w.full = true;
        }
        w.next = (w.next + 1) % capacity;
    }
}

void StatsCollector::drain(EngineMetrics& metrics) {
    std::size_t n = 0;
    while ((n = metrics.frameProcessingUs.pop(scratch_.data(), scratch_.size())) > 0) {
        append(frameWindow_, capacity_, scratch_.data(), n);
    }
    while ((n = metrics.captureCallbackUs.pop(scratch_.data(), scratch_.size())) > 0) {
        append(callbackWindow_, capacity_, scratch_.data(), n);
    }
}

TimingStats StatsCollector::compute(const Window& w) {
    return compute(w.values);
}

TimingStats StatsCollector::compute(const std::vector<float>& window) {
    TimingStats s;
    if (window.empty()) {
        return s;
    }
    std::vector<float> sorted(window);
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](double p) {
        // Nearest-rank percentile.
        const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
        return static_cast<double>(sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1]);
    };
    s.samples = sorted.size();
    s.average = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
    s.p50 = percentile(50.0);
    s.p95 = percentile(95.0);
    s.p99 = percentile(99.0);
    s.max = sorted.back();
    return s;
}

} // namespace vs
