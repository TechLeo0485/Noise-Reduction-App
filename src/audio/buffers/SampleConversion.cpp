#include "audio/buffers/SampleConversion.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {

namespace {

const char* typeName(SampleType t) {
    switch (t) {
    case SampleType::Float32: return "float32";
    case SampleType::Int16: return "int16";
    case SampleType::Int24Packed: return "int24";
    case SampleType::Int32: return "int32";
    default: return "unsupported";
    }
}

template <typename Decode>
void downmix(const std::uint8_t* src, std::size_t frames, std::size_t channels, std::size_t bytesPerSample,
             float* out, Decode decode) noexcept {
    const float scale = 1.0f / static_cast<float>(channels);
    for (std::size_t f = 0; f < frames; ++f) {
        float acc = 0.0f;
        for (std::size_t c = 0; c < channels; ++c) {
            acc += decode(src);
            src += bytesPerSample;
        }
        out[f] = acc * scale;
    }
}

template <typename Encode>
void upmix(const float* mono, std::size_t frames, std::size_t channels, std::size_t bytesPerSample,
           std::uint8_t* dst, Encode encode) noexcept {
    for (std::size_t f = 0; f < frames; ++f) {
        const float v = mono[f];
        for (std::size_t c = 0; c < channels; ++c) {
            encode(v, dst);
            dst += bytesPerSample;
        }
    }
}

} // namespace

std::string StreamFormat::describe() const {
    std::string s = std::to_string(sampleRate) + " Hz, " + std::to_string(channels) + " ch, " + typeName(sampleType);
    if (sampleType == SampleType::Int32 && validBits != 32 && validBits != 0) {
        s += " (" + std::to_string(validBits) + "-bit valid)";
    }
    return s;
}

float int16ToFloat(std::int16_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 32768.0f);
}

std::int16_t floatToInt16(float v) noexcept {
    const float s = std::clamp(v, -1.0f, 1.0f) * 32767.0f;
    return static_cast<std::int16_t>(std::lrintf(s));
}

float int24ToFloat(const std::uint8_t* b) noexcept {
    std::int32_t v = static_cast<std::int32_t>(static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
                                               (static_cast<std::uint32_t>(b[2]) << 16));
    if (v & 0x800000) {
        v |= ~0xFFFFFF; // sign-extend
    }
    return static_cast<float>(v) * (1.0f / 8388608.0f);
}

void floatToInt24(float v, std::uint8_t* b) noexcept {
    const auto s = static_cast<std::int32_t>(std::lrintf(std::clamp(v, -1.0f, 1.0f) * 8388607.0f));
    b[0] = static_cast<std::uint8_t>(s & 0xFF);
    b[1] = static_cast<std::uint8_t>((s >> 8) & 0xFF);
    b[2] = static_cast<std::uint8_t>((s >> 16) & 0xFF);
}

float int32ToFloat(std::int32_t v) noexcept {
    return static_cast<float>(static_cast<double>(v) * (1.0 / 2147483648.0));
}

std::int32_t floatToInt32(float v) noexcept {
    const double s = static_cast<double>(std::clamp(v, -1.0f, 1.0f)) * 2147483647.0;
    return static_cast<std::int32_t>(std::llround(s));
}

void interleavedToMono(const void* source, std::size_t frames, const StreamFormat& format, float* monoOut) noexcept {
    const auto* src = static_cast<const std::uint8_t*>(source);
    const std::size_t ch = format.channels;
    switch (format.sampleType) {
    case SampleType::Float32:
        downmix(src, frames, ch, 4, monoOut, [](const std::uint8_t* p) {
            float v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        });
        break;
    case SampleType::Int16:
        downmix(src, frames, ch, 2, monoOut, [](const std::uint8_t* p) {
            std::int16_t v;
            std::memcpy(&v, p, sizeof(v));
            return int16ToFloat(v);
        });
        break;
    case SampleType::Int24Packed:
        downmix(src, frames, ch, 3, monoOut, [](const std::uint8_t* p) { return int24ToFloat(p); });
        break;
    case SampleType::Int32:
        // 24-in-32 formats are left-justified, so full-scale int32 decoding is correct for both.
        downmix(src, frames, ch, 4, monoOut, [](const std::uint8_t* p) {
            std::int32_t v;
            std::memcpy(&v, p, sizeof(v));
            return int32ToFloat(v);
        });
        break;
    default:
        std::fill(monoOut, monoOut + frames, 0.0f);
        break;
    }
}

void monoToInterleaved(const float* mono, std::size_t frames, const StreamFormat& format, void* destination) noexcept {
    auto* dst = static_cast<std::uint8_t*>(destination);
    const std::size_t ch = format.channels;
    switch (format.sampleType) {
    case SampleType::Float32:
        upmix(mono, frames, ch, 4, dst, [](float v, std::uint8_t* p) { std::memcpy(p, &v, sizeof(v)); });
        break;
    case SampleType::Int16:
        upmix(mono, frames, ch, 2, dst, [](float v, std::uint8_t* p) {
            const std::int16_t s = floatToInt16(v);
            std::memcpy(p, &s, sizeof(s));
        });
        break;
    case SampleType::Int24Packed:
        upmix(mono, frames, ch, 3, dst, [](float v, std::uint8_t* p) { floatToInt24(v, p); });
        break;
    case SampleType::Int32:
        upmix(mono, frames, ch, 4, dst, [](float v, std::uint8_t* p) {
            const std::int32_t s = floatToInt32(v);
            std::memcpy(p, &s, sizeof(s));
        });
        break;
    default:
        std::memset(dst, 0, frames * format.blockAlign);
        break;
    }
}

} // namespace vs
