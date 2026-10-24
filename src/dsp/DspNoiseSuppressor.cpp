#include "dsp/DspNoiseSuppressor.h"

#include "core/DspMath.h"

#include <algorithm>
#include <cmath>

namespace vs {

DspNoiseSuppressor::DspNoiseSuppressor() noexcept
    : attackCoeff_(timeConstantCoeff(3.0f, kBlockRate)),
      releaseCoeff_(timeConstantCoeff(90.0f, kBlockRate)),
      floorFallCoeff_(timeConstantCoeff(40.0f, kBlockRate)) {
    reset();
}

void DspNoiseSuppressor::reset() noexcept {
    // Start high: the minimum tracker falls to the real floor within ~100 ms of the first pause,
    // whereas starting low would take many seconds to rise to typical room-noise levels.
    noiseFloorDb_ = -30.0f;
    gainDb_ = 0.0f;
}

float DspNoiseSuppressor::processFrame(const float* in, float* out, float strength) noexcept {
    constexpr float riseStep = kFloorRiseDbPerSecond / kBlockRate;

    strength = std::clamp(strength, 0.0f, 1.0f);
    const float maxAttenuation = kMaxAttenuationDb * strength;

    for (std::size_t offset = 0; offset < kFrameSize; offset += kSubBlock) {
        const float* src = in + offset;
        float* dst = out + offset;
        const float levelDb = linearToDb(rms(src, kSubBlock));

        // Minimum-statistics style noise floor tracking.
        if (levelDb < noiseFloorDb_) {
            noiseFloorDb_ = levelDb + floorFallCoeff_ * (noiseFloorDb_ - levelDb);
        } else {
            noiseFloorDb_ += riseStep;
        }
        noiseFloorDb_ = std::clamp(noiseFloorDb_, -100.0f, -25.0f);

        const float thresholdDb = noiseFloorDb_ + kMarginDb;
        float targetDb = 0.0f;
        if (levelDb < thresholdDb) {
            targetDb = std::max(-maxAttenuation, -(thresholdDb - levelDb) * 2.0f);
        }

        const float previous = dbToLinear(gainDb_);
        const float coeff = targetDb > gainDb_ ? attackCoeff_ : releaseCoeff_;
        gainDb_ = targetDb + coeff * (gainDb_ - targetDb);
        const float next = dbToLinear(gainDb_);

        const float step = (next - previous) / static_cast<float>(kSubBlock);
        float g = previous;
        for (std::size_t i = 0; i < kSubBlock; ++i) {
            g += step;
            dst[i] = src[i] * g;
        }
    }
    return -1.0f;
}

} // namespace vs
