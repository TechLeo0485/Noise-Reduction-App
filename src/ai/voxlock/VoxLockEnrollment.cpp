#include "ai/voxlock/VoxLockEnrollment.h"

#include "ai/voxlock/VoxLockStream.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>

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

// Start (in samples, from `audio`) of the most representative `seconds` within audio[0, count). 0 if shorter.
std::size_t representativeStart(const float* audio, std::size_t count, double seconds) {
    const std::size_t n = count / kBlock;
    const auto w = static_cast<std::size_t>(seconds * kSampleRate / kBlock);
    if (n <= w) {
        return 0;
    }
    // Per 10 ms block: level in dBFS and whether it clips.
    std::vector<double> level(n);
    std::vector<unsigned char> clipped(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < kBlock; ++j) {
            const float v = audio[i * kBlock + static_cast<std::size_t>(j)];
            s += static_cast<double>(v) * v;
            clipped[i] = clipped[i] != 0 || std::fabs(v) >= 0.99f ? 1 : 0;
        }
        level[i] = dbfs(s / kBlock);
    }
    // Speech: clearly above the noise floor (the quietest 10% of the recording).
    std::vector<double> sorted = level;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 10), sorted.end());
    const double threshold = std::max(sorted[n / 10] + 12.0, -60.0);
    std::vector<double> speech;
    for (double l : level) {
        if (l > threshold) {
            speech.push_back(l);
        }
    }
    double typical = threshold;
    if (!speech.empty()) {
        std::nth_element(speech.begin(), speech.begin() + static_cast<std::ptrdiff_t>(speech.size() / 2), speech.end());
        typical = speech[speech.size() / 2];
    }

    // Slide a w-block window: score = % of speech blocks - distance (dB) of their mean level from typical.
    // Windows with clipping only win when every window clips.
    std::size_t active = 0;
    std::size_t clips = 0;
    double activeLevel = 0.0;
    auto add = [&](std::size_t i, int sign) {
        if (level[i] > threshold) {
            active = sign > 0 ? active + 1 : active - 1;
            activeLevel += sign * level[i];
        }
        clips = sign > 0 ? clips + clipped[i] : clips - clipped[i];
    };
    for (std::size_t i = 0; i < w; ++i) {
        add(i, +1);
    }
    double bestScore = -1e9;
    bool bestClips = true;
    std::size_t bestIndex = 0;
    for (std::size_t start = 0;; ++start) {
        const double mean = active > 0 ? activeLevel / static_cast<double>(active) : threshold;
        const double score = 100.0 * static_cast<double>(active) / static_cast<double>(w) - std::fabs(mean - typical);
        const bool hasClips = clips > 0;
        if ((bestClips && !hasClips) || (hasClips == bestClips && score > bestScore)) {
            bestScore = score;
            bestClips = hasClips;
            bestIndex = start;
        }
        if (start + w >= n) {
            break;
        }
        add(start, -1);
        add(start + w, +1);
    }
    return bestIndex * kBlock;
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

std::vector<float> representativeSample(const std::vector<float>& audio16k, double seconds) {
    const std::size_t length = static_cast<std::size_t>(seconds * kSampleRate);
    if (audio16k.size() <= length) {
        return audio16k;
    }
    const std::size_t begin = representativeStart(audio16k.data(), audio16k.size(), seconds);
    return {audio16k.begin() + static_cast<std::ptrdiff_t>(begin),
            audio16k.begin() + static_cast<std::ptrdiff_t>(std::min(audio16k.size(), begin + length))};
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
    out = representativeSample(audio16k, 3.0);
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

namespace {

constexpr std::size_t kSectionMinSamples = 6 * kSampleRate; // room for a 3 s candidate and a separate 2 s test
constexpr std::size_t kTestSamples = 2 * kSampleRate;
constexpr std::size_t kPieceSamples = kSampleRate; // 1 s pieces of the mixed candidate
constexpr std::size_t kFadeSamples = 80;           // 5 ms fades where the pieces join
constexpr double kChooseMarginDb = 1.0;            // how much better than the most typical window another must be

// Level (dBFS) of each 10 ms block of audio[0, count).
std::vector<double> blockLevels(const float* audio, std::size_t count) {
    std::vector<double> level(count / kBlock);
    for (std::size_t i = 0; i < level.size(); ++i) {
        double s = 0.0;
        for (int j = 0; j < kBlock; ++j) {
            const double v = audio[i * kBlock + static_cast<std::size_t>(j)];
            s += v * v;
        }
        level[i] = dbfs(s / kBlock);
    }
    return level;
}

// Speech threshold: clearly above the noise floor (the quietest 10% of the blocks), as in representativeStart().
double speechThreshold(std::vector<double> level) {
    if (level.empty()) {
        return 0.0;
    }
    const std::size_t q = level.size() / 10;
    std::nth_element(level.begin(), level.begin() + static_cast<std::ptrdiff_t>(q), level.end());
    return std::max(level[q] + 12.0, -60.0);
}

// Share of speech blocks in samples [begin, end).
double speechShare(const std::vector<double>& level, double threshold, std::size_t begin, std::size_t end) {
    const std::size_t b = begin / kBlock;
    const std::size_t e = std::min(level.size(), end / kBlock);
    if (e <= b) {
        return 0.0;
    }
    std::size_t speech = 0;
    for (std::size_t i = b; i < e; ++i) {
        speech += level[i] > threshold ? 1 : 0;
    }
    return static_cast<double>(speech) / static_cast<double>(e - b);
}

bool overlaps(const SampleRange& a, const SampleRange& b) {
    return a.begin < b.end && b.begin < a.end;
}

std::vector<float> slice(const std::vector<float>& audio, SampleRange r) {
    r.end = std::min(r.end, audio.size());
    return {audio.begin() + static_cast<std::ptrdiff_t>(r.begin), audio.begin() + static_cast<std::ptrdiff_t>(r.end)};
}

} // namespace

EnrollmentPlan planEnrollment(const std::vector<float>& audio16k, int maxSections) {
    EnrollmentPlan plan;
    const std::size_t total = audio16k.size();
    const std::size_t windowLen = static_cast<std::size_t>(kEnrollSamples);
    if (total < windowLen) {
        return plan;
    }
    const std::size_t typical = representativeStart(audio16k.data(), total, 3.0);
    SampleCandidate first;
    first.sources = {{typical, typical + windowLen}};
    first.sample = slice(audio16k, first.sources.front());
    first.sample.resize(windowLen, 0.0f);
    first.label = "most typical";
    plan.candidates.push_back(std::move(first));

    const std::size_t sections = std::min<std::size_t>(static_cast<std::size_t>(std::max(maxSections, 0)),
                                                       total / kSectionMinSamples);
    if (sections < 2) {
        return plan;
    }
    const std::vector<double> level = blockLevels(audio16k.data(), total);
    const double threshold = speechThreshold(level);
    const std::size_t sectionLen = total / sections;
    std::vector<SampleRange> pieces;
    for (std::size_t k = 0; k < sections; ++k) {
        const std::size_t begin = k * sectionLen;
        const std::size_t end = k + 1 == sections ? total : begin + sectionLen;
        const std::size_t start = begin + representativeStart(audio16k.data() + begin, end - begin, 3.0);
        const SampleRange window{start, start + windowLen};
        if (speechShare(level, threshold, window.begin, window.end) < 0.5) {
            continue; // a pause or silence: nothing to learn from this section
        }

        // Held-out test speech: the 2 s with the most speech in this section, away from its candidate window.
        SampleRange test;
        double bestShare = 0.5;
        for (std::size_t t = begin; t + kTestSamples <= end; t += kBlock * 10) {
            const SampleRange r{t, t + kTestSamples};
            if (overlaps(r, window)) {
                continue;
            }
            const double share = speechShare(level, threshold, r.begin, r.end);
            if (share > bestShare) {
                bestShare = share;
                test = r;
            }
        }
        if (test.end > test.begin) {
            plan.tests.push_back(test);
        }

        const SampleRange& typicalWindow = plan.candidates.front().sources.front();
        const std::size_t shared = overlaps(window, typicalWindow)
                                       ? std::min(window.end, typicalWindow.end) - std::max(window.begin, typicalWindow.begin)
                                       : 0;
        if (shared < windowLen / 2) { // otherwise the same audio as the first candidate
            SampleCandidate c;
            c.sources = {window};
            c.sample = slice(audio16k, window);
            c.sample.resize(windowLen, 0.0f);
            c.label = "section " + std::to_string(k + 1);
            plan.candidates.push_back(std::move(c));
        }
        const std::size_t pieceStart = start + representativeStart(audio16k.data() + start, windowLen, 1.0);
        pieces.push_back({pieceStart, pieceStart + kPieceSamples});
    }

    // A mix of 1 s from the start, the middle and the end of the recording (e.g. normal, quiet or high, free talk).
    if (pieces.size() >= 3) {
        SampleCandidate mix;
        mix.label = "mix";
        for (std::size_t idx : {std::size_t{0}, pieces.size() / 2, pieces.size() - 1}) {
            std::vector<float> piece = slice(audio16k, pieces[idx]);
            piece.resize(kPieceSamples, 0.0f);
            for (std::size_t i = 0; i < kFadeSamples; ++i) {
                const float g = static_cast<float>(i) / static_cast<float>(kFadeSamples);
                piece[i] *= g;
                piece[piece.size() - 1 - i] *= g;
            }
            mix.sample.insert(mix.sample.end(), piece.begin(), piece.end());
            mix.sources.push_back(pieces[idx]);
        }
        mix.sample.resize(windowLen, 0.0f);
        plan.candidates.push_back(std::move(mix));
    }
    return plan;
}

double keptDb(const float* input, const float* output, std::size_t count) {
    const std::vector<double> level = blockLevels(input, count);
    const double threshold = speechThreshold(level);
    double in = 0.0;
    double out = 0.0;
    for (std::size_t i = 0; i < level.size(); ++i) {
        if (level[i] <= threshold) {
            continue;
        }
        for (int j = 0; j < kBlock; ++j) {
            const std::size_t at = i * kBlock + static_cast<std::size_t>(j);
            in += static_cast<double>(input[at]) * input[at];
            out += static_cast<double>(output[at]) * output[at];
        }
    }
    if (in <= 0.0) {
        return 0.0;
    }
    return std::clamp(10.0 * std::log10((out + 1e-12) / in), -40.0, 6.0);
}

std::size_t chooseCandidate(const std::vector<float>& audio16k, const EnrollmentPlan& plan, const IsolateFn& isolate,
                            const std::function<void(std::size_t, std::size_t)>& progress, std::vector<double>* scoresOut,
                            int parallel) {
    const std::size_t count = plan.candidates.size();
    if (scoresOut != nullptr) {
        scoresOut->assign(count, -1e9);
    }
    if (count <= 1 || plan.tests.empty()) {
        return 0;
    }
    // Judge each candidate on speech it was not built from (all tests if that leaves none).
    std::vector<std::vector<std::vector<float>>> inputs(count);
    for (std::size_t c = 0; c < count; ++c) {
        const SampleCandidate& candidate = plan.candidates[c];
        for (const SampleRange& t : plan.tests) {
            if (std::none_of(candidate.sources.begin(), candidate.sources.end(),
                             [&](const SampleRange& s) { return overlaps(s, t); })) {
                inputs[c].push_back(slice(audio16k, t));
            }
        }
        if (inputs[c].empty()) {
            for (const SampleRange& t : plan.tests) {
                inputs[c].push_back(slice(audio16k, t));
            }
        }
    }

    // Candidates run `parallel` at a time; each builds its own session, which is the slow part.
    std::vector<std::vector<std::vector<float>>> outputs(count);
    std::vector<unsigned char> ok(count, 0);
    const std::size_t batch = static_cast<std::size_t>(std::max(1, parallel));
    for (std::size_t first = 0; first < count; first += batch) {
        if (progress) {
            progress(first, count);
        }
        const std::size_t last = std::min(count, first + batch);
        std::vector<std::thread> threads;
        for (std::size_t c = first + 1; c < last; ++c) {
            threads.emplace_back([&, c] { ok[c] = isolate(plan.candidates[c].sample, inputs[c], outputs[c]) ? 1 : 0; });
        }
        ok[first] = isolate(plan.candidates[first].sample, inputs[first], outputs[first]) ? 1 : 0;
        for (std::thread& t : threads) {
            t.join();
        }
    }

    std::size_t best = 0;
    double bestScore = -1e9;
    for (std::size_t c = 0; c < count; ++c) {
        if (ok[c] == 0 || outputs[c].size() != inputs[c].size()) {
            continue;
        }
        double sum = 0.0;
        double worst = 1e9;
        bool failed = false;
        for (std::size_t i = 0; i < inputs[c].size(); ++i) {
            if (outputs[c][i].size() < inputs[c][i].size()) {
                failed = true;
                break;
            }
            // Keeping more than all of it earns nothing: the aim is not to lose any of the speaker.
            const double kept = std::min(0.0, keptDb(inputs[c][i].data(), outputs[c][i].data(), inputs[c][i].size()));
            sum += kept;
            worst = std::min(worst, kept);
        }
        if (failed) {
            continue;
        }
        const double score = sum / static_cast<double>(inputs[c].size()) + worst;
        if (scoresOut != nullptr) {
            (*scoresOut)[c] = score;
        }
        // The most typical window is what the network was benchmarked with; another candidate has to be clearly
        // better (it is only tested on this speaker, not on how well it rejects other voices).
        const double margin = c == 0 ? 0.0 : kChooseMarginDb;
        if (score - margin > bestScore) {
            bestScore = score - margin;
            best = c;
        }
    }
    return best;
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

bool saveRecording(const std::filesystem::path& path, const std::vector<float>& audio16k, std::string& error) {
    const auto dataBytes = static_cast<std::uint32_t>(audio16k.size() * sizeof(std::int16_t));
    std::vector<char> file;
    file.reserve(44 + dataBytes);
    file.insert(file.end(), {'R', 'I', 'F', 'F'});
    put<std::uint32_t>(file, 36 + dataBytes);
    file.insert(file.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put<std::uint32_t>(file, 16);
    put<std::uint16_t>(file, 1); // WAVE_FORMAT_PCM
    put<std::uint16_t>(file, 1);
    put<std::uint32_t>(file, kSampleRate);
    put<std::uint32_t>(file, kSampleRate * 2);
    put<std::uint16_t>(file, 2);
    put<std::uint16_t>(file, 16);
    file.insert(file.end(), {'d', 'a', 't', 'a'});
    put<std::uint32_t>(file, dataBytes);
    for (float v : audio16k) {
        const float clamped = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
        put<std::int16_t>(file, static_cast<std::int16_t>(std::lround(clamped * 32767.0f)));
    }

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
            const bool isFloat = format == 3 && bits == 32;
            const bool isPcm16 = format == 1 && bits == 16;
            if (!(isFloat || isPcm16) || channels != 1 || rate != static_cast<std::uint32_t>(kSampleRate)) {
                error = "The saved voice sample has an unexpected format.";
                return false;
            }
            if (size > 64u * 1024u * 1024u) {
                error = "The saved voice sample is too large.";
                return false;
            }
            if (isFloat) {
                audio16k.resize(size / sizeof(float));
                in.read(reinterpret_cast<char*>(audio16k.data()),
                        static_cast<std::streamsize>(audio16k.size() * sizeof(float)));
            } else {
                std::vector<std::int16_t> pcm(size / sizeof(std::int16_t));
                in.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));
                audio16k.resize(pcm.size());
                std::transform(pcm.begin(), pcm.end(), audio16k.begin(),
                               [](std::int16_t v) { return static_cast<float>(v) / 32768.0f; });
            }
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
