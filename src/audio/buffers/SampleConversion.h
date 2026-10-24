#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vs {

enum class SampleType : std::uint8_t {
    Unsupported,
    Float32,
    Int16,
    Int24Packed, // 3 bytes per sample
    Int32,       // 32-bit container; validBits may be 24 (left-justified) or 32
};

// Endpoint stream format as understood by the conversion layer.
struct StreamFormat {
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;  // container size
    std::uint16_t validBits = 0;
    std::uint16_t blockAlign = 0;     // bytes per frame
    std::uint32_t channelMask = 0;
    SampleType sampleType = SampleType::Unsupported;

    [[nodiscard]] bool isSupported() const noexcept {
        return sampleType != SampleType::Unsupported && channels > 0 && sampleRate > 0 &&
               blockAlign == channels * (bitsPerSample / 8);
    }
    [[nodiscard]] std::string describe() const;
};

// Converts interleaved device samples to mono float by averaging all channels.
// `source` holds `frames` frames in `format`. Real-time safe.
void interleavedToMono(const void* source, std::size_t frames, const StreamFormat& format, float* monoOut) noexcept;

// Writes mono float samples to every channel of an interleaved device buffer. Real-time safe.
void monoToInterleaved(const float* mono, std::size_t frames, const StreamFormat& format, void* destination) noexcept;

// Individual sample codecs (exposed for tests).
[[nodiscard]] float int16ToFloat(std::int16_t v) noexcept;
[[nodiscard]] std::int16_t floatToInt16(float v) noexcept;
[[nodiscard]] float int24ToFloat(const std::uint8_t* bytes) noexcept;
void floatToInt24(float v, std::uint8_t* bytes) noexcept;
[[nodiscard]] float int32ToFloat(std::int32_t v) noexcept;
[[nodiscard]] std::int32_t floatToInt32(float v) noexcept;

} // namespace vs
