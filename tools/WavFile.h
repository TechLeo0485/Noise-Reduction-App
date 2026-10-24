#pragma once

// Minimal WAV reader/writer for the command-line tools (PCM16, PCM24, PCM32, float32).
// Not used by the application itself.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace vs::tools {

struct WavData {
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::vector<float> mono; // channels averaged
};

inline bool readWav(const std::string& path, WavData& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    char riff[12];
    f.read(riff, 12);
    if (!f || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }
    std::uint16_t format = 0, bits = 0;
    bool haveFmt = false;
    while (f) {
        char id[4];
        std::uint32_t size = 0;
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&size), 4);
        if (!f) {
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0) {
            std::vector<char> fmt(size);
            f.read(fmt.data(), size);
            std::memcpy(&format, fmt.data(), 2);
            std::memcpy(&out.channels, fmt.data() + 2, 2);
            std::memcpy(&out.sampleRate, fmt.data() + 4, 4);
            std::memcpy(&bits, fmt.data() + 14, 2);
            if (format == 0xFFFE && size >= 26) {
                std::memcpy(&format, fmt.data() + 24, 2); // sub-format GUID starts with the tag
            }
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0 && haveFmt) {
            std::vector<std::uint8_t> raw(size);
            f.read(reinterpret_cast<char*>(raw.data()), size);
            const std::size_t bytesPerSample = bits / 8;
            if (out.channels == 0 || bytesPerSample == 0) {
                error = "invalid format";
                return false;
            }
            const std::size_t frames = raw.size() / (bytesPerSample * out.channels);
            out.mono.assign(frames, 0.0f);
            for (std::size_t i = 0; i < frames; ++i) {
                float acc = 0.0f;
                for (std::size_t c = 0; c < out.channels; ++c) {
                    const std::uint8_t* p = raw.data() + (i * out.channels + c) * bytesPerSample;
                    float v = 0.0f;
                    if (format == 3 && bits == 32) {
                        std::memcpy(&v, p, 4);
                    } else if (format == 1 && bits == 16) {
                        std::int16_t s;
                        std::memcpy(&s, p, 2);
                        v = s / 32768.0f;
                    } else if (format == 1 && bits == 24) {
                        std::int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                        if (s & 0x800000) {
                            s |= ~0xFFFFFF;
                        }
                        v = s / 8388608.0f;
                    } else if (format == 1 && bits == 32) {
                        std::int32_t s;
                        std::memcpy(&s, p, 4);
                        v = static_cast<float>(s / 2147483648.0);
                    } else {
                        error = "unsupported WAV sample format";
                        return false;
                    }
                    acc += v;
                }
                out.mono[i] = acc / out.channels;
            }
            return true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
    error = "no data chunk";
    return false;
}

inline bool writeWavFloatMono(const std::string& path, const std::vector<float>& samples, std::uint32_t sampleRate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    const std::uint32_t riffSize = 36 + dataBytes;
    const std::uint16_t format = 3, channels = 1, bits = 32, blockAlign = 4;
    const std::uint32_t byteRate = sampleRate * blockAlign, fmtSize = 16;
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riffSize), 4);
    f.write("WAVEfmt ", 8);
    f.write(reinterpret_cast<const char*>(&fmtSize), 4);
    f.write(reinterpret_cast<const char*>(&format), 2);
    f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&sampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    f.write(reinterpret_cast<const char*>(&blockAlign), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataBytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    return static_cast<bool>(f);
}

} // namespace vs::tools
