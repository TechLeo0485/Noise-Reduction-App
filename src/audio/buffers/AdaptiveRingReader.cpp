#include "audio/buffers/AdaptiveRingReader.h"

#include "audio/monitoring/EngineMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

void AdaptiveRingReader::configure(const Config& config) {
    config_ = config;
    targetSamples_ = config.targetMs * 0.001 * kInternalRate;
    maxSamples_ = targetSamples_ + config.maxExtraMs * 0.001 * kInternalRate;

    // Worst-case input needed for one device callback, with headroom for drift correction.
    const auto maxInput = static_cast<std::size_t>(
        std::ceil(static_cast<double>(config.maxDeviceFrames) * kInternalRate / config.deviceRate * (1.0 + kMaxCorrection))) + 64;
    resampler_.configure(kInternalRate, config.deviceRate, maxInput);
    staging_.assign(maxInput, 0.0f);
    reset();
}

void AdaptiveRingReader::reset() noexcept {
    resampler_.reset();
    smoothedBacklog_ = targetSamples_;
    integral_ = 0.0;
    priming_ = true;
    fadeGain_ = 0.0f;
}

double AdaptiveRingReader::resamplerLatencyMs() const noexcept {
    return resampler_.latencyInputSamples() * 1000.0 / kInternalRate;
}

void AdaptiveRingReader::render(SpscRingBuffer<float>& ring, float* out, std::size_t frames,
                                RenderPathMetrics& metrics) noexcept {
    if (frames == 0) {
        return;
    }
    std::size_t available = ring.available();
    double backlog = static_cast<double>(available) + static_cast<double>(resampler_.bufferedInput());

    // Hard limit: never let latency accumulate.
    if (backlog > maxSamples_) {
        const auto excess = static_cast<std::size_t>(backlog - targetSamples_);
        ring.skip(std::min(excess, available));
        available = ring.available();
        backlog = static_cast<double>(available) + static_cast<double>(resampler_.bufferedInput());
        smoothedBacklog_ = backlog;
        integral_ = 0.0;
        metrics.latencyResets.fetch_add(1, std::memory_order_relaxed);
    }

    if (priming_) {
        if (backlog < targetSamples_) {
            std::memset(out, 0, frames * sizeof(float));
            metrics.bufferedMs.store(static_cast<float>(backlog * 1000.0 / kInternalRate), std::memory_order_relaxed);
            return;
        }
        priming_ = false;
        fadeGain_ = 0.0f;
        smoothedBacklog_ = backlog;
        integral_ = 0.0;
    }

    // Drift control: steer the smoothed backlog towards the target.
    smoothedBacklog_ += 0.02 * (backlog - smoothedBacklog_);
    const double errorSeconds = (smoothedBacklog_ - targetSamples_) / kInternalRate;
    const double dt = static_cast<double>(frames) / config_.deviceRate;
    integral_ = std::clamp(integral_ + errorSeconds * dt, -0.05, 0.05);
    const double correction =
        std::clamp(kProportionalGain * errorSeconds + kIntegralGain * integral_, -kMaxCorrection, kMaxCorrection);
    resampler_.setStepScale(1.0 + correction);

    const std::size_t needed = resampler_.inputRequiredFor(frames);
    const std::size_t take = std::min({needed, available, staging_.size(), resampler_.freeInputSpace()});
    const std::size_t popped = ring.pop(staging_.data(), take);
    resampler_.write(staging_.data(), popped);
    const std::size_t produced = resampler_.read(out, frames);

    if (produced < frames) {
        // Underrun: fade out what we have, pad with silence, then re-prime.
        const std::size_t fade = std::min(produced, kFadeSamples);
        for (std::size_t i = 0; i < fade; ++i) {
            out[produced - fade + i] *= 1.0f - static_cast<float>(i + 1) / static_cast<float>(fade);
        }
        std::memset(out + produced, 0, (frames - produced) * sizeof(float));
        metrics.underruns.fetch_add(1, std::memory_order_relaxed);
        priming_ = true;
    }

    if (fadeGain_ < 1.0f) {
        const float step = 1.0f / static_cast<float>(kFadeSamples);
        for (std::size_t i = 0; i < produced && fadeGain_ < 1.0f; ++i) {
            fadeGain_ = std::min(1.0f, fadeGain_ + step);
            out[i] *= fadeGain_;
        }
    }

    metrics.framesRendered.fetch_add(frames, std::memory_order_relaxed);
    metrics.bufferedMs.store(static_cast<float>(smoothedBacklog_ * 1000.0 / kInternalRate), std::memory_order_relaxed);
    metrics.driftCorrectionPpm.store(static_cast<float>(correction * 1.0e6), std::memory_order_relaxed);
}

} // namespace vs
