#include "ai/voxlock/VoxLockEnrollment.h"

#include "ai/voxlock/VoxLockStream.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace vs::voxlock {

namespace {

constexpr int kBlock = 160; // 10 ms at 16 kHz

double dbfs(double meanSquare) {
    return 10.0 * std::log10(meanSquare + 1e-12);
}

template <typename T>
void put(std::vector<char>& out, T value) {
    const auto* p = reinterpret_cast<const char*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

} // namespace

std::vector<float> loudSample(const std::vector<float>& audio16k, double seconds, double withinSeconds) {
    const std::size_t segLen = std::min(audio16k.size(), static_cast<std::size_t>(withinSeconds * kSampleRate));
    const std::size_t n = segLen / kBlock;
    const auto w = static_cast<std::size_t>(seconds * kSampleRate / kBlock);
    if (n <= w) {
        return {audio16k.begin(), audio16k.begin() + static_cast<std::ptrdiff_t>(segLen)};
    }
    // e = mean square per 10 ms block; the window start is the first argmax of the w-block moving sum.
    std::vector<double> e(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < kBlock; ++j) {
            const double v = audio16k[i * kBlock + static_cast<std::size_t>(j)];
            s += v * v;
        }
        e[i] = s / kBlock;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < w; ++i) {
        sum += e[i];
    }
    double best = sum;
    std::size_t bestIndex = 0;
    for (std::size_t i = 1; i + w <= n; ++i) {
        sum += e[i + w - 1] - e[i - 1];
        if (sum > best) {
            best = sum;
            bestIndex = i;
        }
    }
    const std::size_t start = bestIndex * kBlock;
    const std::size_t length = std::min(static_cast<std::size_t>(seconds * kSampleRate), segLen - start);
    return {audio16k.begin() + static_cast<std::ptrdiff_t>(start),
            audio16k.begin() + static_cast<std::ptrdiff_t>(start + length)};
}

bool fitSample(const std::vector<float>& audio16k, std::vector<float>& out, std::string& error) {
    out.clear();
    if (audio16k.size() < static_cast<std::size_t>(kEnrollSamples)) {
        error = "The voice sample must be at least 3 seconds long.";
        return false;
    }
    if (audio16k.size() == static_cast<std::size_t>(kEnrollSamples)) {
        out = audio16k;
        return true;
    }
    out = loudSample(audio16k, 3.0, 30.0);
    out.resize(kEnrollSamples, 0.0f);
    return true;
}

bool validateVoiceSample(const std::vector<float>& audio16k, std::string& error) {
    std::vector<float> sample;
    if (!fitSample(audio16k, sample, error)) {
        return false;
    }
    double sum = 0.0;
    std::size_t clipped = 0;
    for (float v : sample) {
        if (!std::isfinite(v)) {
            error = "The recording contains invalid samples. Please record again.";
            return false;
        }
        sum += static_cast<double>(v) * v;
        clipped += std::fabs(v) >= 0.999f ? 1 : 0;
    }
    const double levelDb = dbfs(sum / static_cast<double>(sample.size()));
    if (levelDb < -45.0) {
        error = "Your voice was too quiet (" + std::to_string(static_cast<int>(std::lround(levelDb))) +
                " dBFS). Move closer to the microphone or raise the microphone gain, then record again.";
        return false;
    }
    if (clipped > sample.size() / 200) {
        error = "The recording is distorted (clipping). Lower the microphone gain and record again.";
        return false;
    }
    // Speech has pauses: in the loudest 3 s the quietest blocks should sit well below the loudest ones.
    // A flat level means steady noise (fan, hum) rather than a voice.
    std::vector<double> blocks;
    for (std::size_t i = 0; i + kBlock <= sample.size(); i += kBlock) {
        double s = 0.0;
        for (int j = 0; j < kBlock; ++j) {
            s += static_cast<double>(sample[i + static_cast<std::size_t>(j)]) * sample[i + static_cast<std::size_t>(j)];
        }
        blocks.push_back(dbfs(s / kBlock));
    }
    std::sort(blocks.begin(), blocks.end());
    const double quiet = blocks[blocks.size() / 10];
    const double loud = blocks[blocks.size() * 9 / 10];
    if (loud - quiet < 10.0) {
        error = "No clear speech was detected - only steady sound. Speak continuously in a quiet room and record again.";
        return false;
    }
    return true;
}

bool saveVoiceSample(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error) {
    const auto dataBytes = static_cast<std::uint32_t>(audio16k.size() * sizeof(float));
    std::vector<char> file;
    file.reserve(64 + dataBytes);
    file.insert(file.end(), {'R', 'I', 'F', 'F'});
    put<std::uint32_t>(file, 4 + 26 + 12 + 8 + dataBytes);
    file.insert(file.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put<std::uint32_t>(file, 18);
    put<std::uint16_t>(file, 3); // WAVE_FORMAT_IEEE_FLOAT
    put<std::uint16_t>(file, 1);
    put<std::uint32_t>(file, kSampleRate);
    put<std::uint32_t>(file, kSampleRate * 4);
    put<std::uint16_t>(file, 4);
    put<std::uint16_t>(file, 32);
    put<std::uint16_t>(file, 0);
    file.insert(file.end(), {'f', 'a', 'c', 't'});
    put<std::uint32_t>(file, 4);
    put<std::uint32_t>(file, static_cast<std::uint32_t>(audio16k.size()));
    file.insert(file.end(), {'d', 'a', 't', 'a'});
    put<std::uint32_t>(file, dataBytes);
    const auto* bytes = reinterpret_cast<const char*>(audio16k.data());
    file.insert(file.end(), bytes, bytes + dataBytes);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temp = path;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        if (!out) {
            error = "Cannot write " + temp.string();
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec);
        error = "Cannot replace " + path.string();
        return false;
    }
    return true;
}

bool loadVoiceSample(const std::filesystem::path& path, std::vector<float>& audio16k, std::string& error) {
    audio16k.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "No voice sample at " + path.string();
        return false;
    }
    char riff[12];
    in.read(riff, 12);
    if (!in || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        error = "The saved voice sample is not a WAV file.";
        return false;
    }
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    while (in) {
        char id[4];
        std::uint32_t size = 0;
        in.read(id, 4);
        in.read(reinterpret_cast<char*>(&size), 4);
        if (!in) {
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            std::vector<char> fmt(size);
            in.read(fmt.data(), size);
            std::memcpy(&format, fmt.data(), 2);
            std::memcpy(&channels, fmt.data() + 2, 2);
            std::memcpy(&rate, fmt.data() + 4, 4);
            std::memcpy(&bits, fmt.data() + 14, 2);
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (format != 3 || channels != 1 || bits != 32 || rate != static_cast<std::uint32_t>(kSampleRate)) {
                error = "The saved voice sample has an unexpected format.";
                return false;
            }
            if (size > 64u * 1024u * 1024u) {
                error = "The saved voice sample is too large.";
                return false;
            }
            audio16k.resize(size / sizeof(float));
            in.read(reinterpret_cast<char*>(audio16k.data()), static_cast<std::streamsize>(audio16k.size() * sizeof(float)));
            if (!in) {
                audio16k.clear();
                error = "The saved voice sample is truncated.";
                return false;
            }
            return true;
        } else {
            in.seekg(size + (size & 1u), std::ios::cur);
        }
    }
    error = "The saved voice sample has no audio data.";
    return false;
}

} // namespace vs::voxlock
