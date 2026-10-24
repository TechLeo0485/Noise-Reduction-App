#include "diagnostics/LoopbackLatencyTester.h"

#include "audio/buffers/SampleConversion.h"
#include "audio/wasapi/WasapiCommon.h"
#include "core/DspMath.h"
#include "devices/DeviceManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vs {

namespace {

constexpr REFERENCE_TIME kBufferDuration = 1000000; // 100 ms
constexpr double kLeadInMs = 400.0;
constexpr double kTailMs = 600.0;
constexpr double kMaxQueuedMs = 30.0;

struct ClockSample {
    double devicePositionFrames;
    double qpc100ns;
};

struct PacketStamp {
    std::size_t firstIndex;
    double qpc100ns;
};

struct OpenedClient {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    UniqueWaveFormat format;
    StreamFormat streamFormat;
    UINT32 bufferFrames = 0;
};

HRESULT openPolledClient(IMMDeviceEnumerator* enumerator, const std::wstring& id, EDataFlow flow, OpenedClient& out,
                         std::string& error) {
    HRESULT hr = openEndpoint(enumerator, id, flow, out.device);
    if (FAILED(hr)) {
        error = std::string(flow == eRender ? "Output" : "Input") + " device unavailable: " + hresultToString(hr);
        return hr;
    }
    hr = out.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &out.client);
    if (FAILED(hr)) {
        error = "Activate failed: " + hresultToString(hr);
        return hr;
    }
    WAVEFORMATEX* raw = nullptr;
    hr = out.client->GetMixFormat(&raw);
    if (FAILED(hr)) {
        error = "GetMixFormat failed: " + hresultToString(hr);
        return hr;
    }
    out.format.reset(raw);
    out.streamFormat = toStreamFormat(raw);
    if (!out.streamFormat.isSupported()) {
        error = "Unsupported mix format: " + out.streamFormat.describe();
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    hr = out.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, kBufferDuration, 0, raw, nullptr);
    if (FAILED(hr)) {
        error = "Initialize failed: " + hresultToString(hr);
        return hr;
    }
    return out.client->GetBufferSize(&out.bufferFrames);
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t mid = v.size() / 2;
    return v.size() % 2 == 1 ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
}

} // namespace

LatencyTestResult runLoopbackLatencyTest(const LatencyTestOptions& options) {
    LatencyTestResult result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        result.error = "Cannot create device enumerator: " + hresultToString(hr);
        return result;
    }

    OpenedClient render;
    OpenedClient capture;
    if (FAILED(openPolledClient(enumerator.Get(), options.outputDeviceId, eRender, render, result.error)) ||
        FAILED(openPolledClient(enumerator.Get(), options.inputDeviceId, eCapture, capture, result.error))) {
        return result;
    }
    result.outputName = toUtf8(deviceFriendlyName(render.device.Get()));
    result.inputName = toUtf8(deviceFriendlyName(capture.device.Get()));
    result.outputFormat = render.streamFormat.describe();
    result.inputFormat = capture.streamFormat.describe();

    ComPtr<IAudioRenderClient> renderClient;
    ComPtr<IAudioCaptureClient> captureClient;
    ComPtr<IAudioClock> clock;
    if (FAILED(render.client->GetService(__uuidof(IAudioRenderClient), &renderClient)) ||
        FAILED(render.client->GetService(__uuidof(IAudioClock), &clock)) ||
        FAILED(capture.client->GetService(__uuidof(IAudioCaptureClient), &captureClient))) {
        result.error = "Failed to obtain WASAPI services";
        return result;
    }
    UINT64 clockFrequency = 0;
    clock->GetFrequency(&clockFrequency);

    const double renderRate = render.streamFormat.sampleRate;
    const double captureRate = capture.streamFormat.sampleRate;
    const double totalMs = kLeadInMs + options.pulses * options.intervalMs + kTailMs;
    const auto totalRenderFrames = static_cast<std::size_t>(totalMs * 0.001 * renderRate);
    const auto pulseFrames = static_cast<std::size_t>(options.pulseMs * 0.001 * renderRate);
    const auto rampFrames = std::max<std::size_t>(4, static_cast<std::size_t>(0.0002 * renderRate));

    std::vector<std::size_t> pulseStarts;
    for (int k = 0; k < options.pulses; ++k) {
        pulseStarts.push_back(static_cast<std::size_t>((kLeadInMs + k * options.intervalMs) * 0.001 * renderRate));
    }

    std::vector<float> captured(static_cast<std::size_t>((totalMs + 500.0) * 0.001 * captureRate), 0.0f);
    std::size_t capturedCount = 0;
    std::vector<PacketStamp> stamps;
    std::vector<ClockSample> clockSamples;
    std::vector<float> renderScratch(render.bufferFrames);
    std::vector<float> captureScratch(capture.bufferFrames * 2 + 64);
    stamps.reserve(4096);
    clockSamples.reserve(4096);

    if (FAILED(capture.client->Start()) || FAILED(render.client->Start())) {
        capture.client->Stop();
        render.client->Stop();
        result.error = "Failed to start test streams";
        return result;
    }

    std::size_t written = 0;
    const auto maxQueued = static_cast<UINT32>(kMaxQueuedMs * 0.001 * renderRate);
    const double twoPi = 2.0 * 3.14159265358979323846;
    bool failed = false;

    while (!failed && (written < totalRenderFrames)) {
        // ---- render ----
        UINT32 padding = 0;
        if (FAILED(render.client->GetCurrentPadding(&padding))) {
            failed = true;
            break;
        }
        if (padding < maxQueued) {
            const UINT32 frames = std::min<UINT32>(maxQueued - padding, static_cast<UINT32>(renderScratch.size()));
            BYTE* data = nullptr;
            if (frames > 0 && SUCCEEDED(renderClient->GetBuffer(frames, &data))) {
                for (UINT32 i = 0; i < frames; ++i) {
                    const std::size_t n = written + i;
                    float v = 0.0f;
                    for (std::size_t start : pulseStarts) {
                        if (n >= start && n < start + pulseFrames) {
                            const std::size_t t = n - start;
                            double env = 1.0;
                            if (t < rampFrames) {
                                env = 0.5 - 0.5 * std::cos(3.14159265358979323846 * t / rampFrames);
                            } else if (t >= pulseFrames - rampFrames) {
                                env = 0.5 - 0.5 * std::cos(3.14159265358979323846 * (pulseFrames - t) / rampFrames);
                            }
                            v = static_cast<float>(options.amplitude * env * std::sin(twoPi * options.toneHz * t / renderRate));
                            break;
                        }
                    }
                    renderScratch[i] = v;
                }
                monoToInterleaved(renderScratch.data(), frames, render.streamFormat, data);
                renderClient->ReleaseBuffer(frames, 0);
                written += frames;
            }
        }
        UINT64 position = 0, qpc = 0;
        if (clockFrequency > 0 && SUCCEEDED(clock->GetPosition(&position, &qpc)) && clockSamples.size() < clockSamples.capacity()) {
            clockSamples.push_back({static_cast<double>(position) * renderRate / static_cast<double>(clockFrequency),
                                    static_cast<double>(qpc)});
        }

        // ---- capture ----
        UINT32 packet = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition))) {
                failed = true;
                break;
            }
            const std::size_t usable = std::min<std::size_t>({frames, captureScratch.size(), captured.size() - capturedCount});
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                std::fill(captured.begin() + static_cast<std::ptrdiff_t>(capturedCount),
                          captured.begin() + static_cast<std::ptrdiff_t>(capturedCount + usable), 0.0f);
            } else {
                interleavedToMono(data, usable, capture.streamFormat, captured.data() + capturedCount);
            }
            if (stamps.size() < stamps.capacity()) {
                stamps.push_back({capturedCount, static_cast<double>(qpcPosition)});
            }
            capturedCount += usable;
            captureClient->ReleaseBuffer(frames);
        }
        Sleep(2);
    }

    // Keep capturing through the tail so the last pulse is heard.
    const ULONGLONG tailEnd = GetTickCount64() + static_cast<ULONGLONG>(kTailMs);
    while (!failed && GetTickCount64() < tailEnd) {
        UINT32 packet = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition))) {
                failed = true;
                break;
            }
            const std::size_t usable = std::min<std::size_t>(frames, captured.size() - capturedCount);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                interleavedToMono(data, usable, capture.streamFormat, captured.data() + capturedCount);
            }
            if (stamps.size() < stamps.capacity()) {
                stamps.push_back({capturedCount, static_cast<double>(qpcPosition)});
            }
            capturedCount += usable;
            captureClient->ReleaseBuffer(frames);
        }
        Sleep(2);
    }
    render.client->Stop();
    capture.client->Stop();

    if (failed) {
        result.error = "A test stream failed while running (device removed?)";
        return result;
    }
    if (stamps.empty() || clockSamples.empty() || capturedCount == 0) {
        result.error = "No audio was captured from the input device";
        return result;
    }

    // ---- Analysis ----------------------------------------------------------------------
    const auto renderPlayTime = [&](std::size_t frame) {
        // Use the clock sample whose device position is closest to the pulse.
        const ClockSample* best = &clockSamples.front();
        for (const ClockSample& s : clockSamples) {
            if (std::fabs(s.devicePositionFrames - frame) < std::fabs(best->devicePositionFrames - frame)) {
                best = &s;
            }
        }
        return best->qpc100ns + (static_cast<double>(frame) - best->devicePositionFrames) / renderRate * 1.0e7;
    };
    const auto captureTime = [&](std::size_t index) {
        const PacketStamp* stamp = &stamps.front();
        for (const PacketStamp& s : stamps) {
            if (s.firstIndex <= index) {
                stamp = &s;
            } else {
                break;
            }
        }
        return stamp->qpc100ns + static_cast<double>(index - stamp->firstIndex) / captureRate * 1.0e7;
    };

    // Noise floor from the lead-in (before any pulse can arrive).
    const auto floorSamples = std::min(capturedCount, static_cast<std::size_t>(0.25 * captureRate));
    const float floorRms = rms(captured.data(), floorSamples);
    result.noiseFloorDb = linearToDb(floorRms);
    result.peakDb = linearToDb(peakAbs(captured.data(), capturedCount));
    const float threshold = std::max(floorRms * dbToLinear(static_cast<float>(options.thresholdAboveFloorDb)), 0.005f);

    result.pulsesSent = options.pulses;
    std::size_t searchFrom = 0;
    for (std::size_t start : pulseStarts) {
        const double tPlay = renderPlayTime(start);
        for (std::size_t i = searchFrom; i < capturedCount; ++i) {
            const double t = captureTime(i);
            if (t < tPlay - 1.0e5) { // allow 10 ms of timestamp jitter before the pulse
                continue;
            }
            if (t > tPlay + options.intervalMs * 0.9 * 1.0e4) {
                break;
            }
            if (std::fabs(captured[i]) >= threshold) {
                result.latenciesMs.push_back((t - tPlay) / 1.0e4);
                // Skip past this pulse.
                searchFrom = i + static_cast<std::size_t>(options.pulseMs * 2.0 * 0.001 * captureRate);
                break;
            }
        }
    }
    result.pulsesDetected = static_cast<int>(result.latenciesMs.size());
    if (result.pulsesDetected == 0) {
        result.error = "Test pulses were not detected on the input. Check that the output and input are connected "
                       "(e.g. CABLE Input -> CABLE Output) and that the input is not muted.";
        return result;
    }
    result.medianMs = median(result.latenciesMs);
    result.minMs = *std::min_element(result.latenciesMs.begin(), result.latenciesMs.end());
    result.maxMs = *std::max_element(result.latenciesMs.begin(), result.latenciesMs.end());
    result.ok = result.pulsesDetected >= std::max(1, options.pulses / 2);
    if (!result.ok) {
        result.error = "Only " + std::to_string(result.pulsesDetected) + " of " + std::to_string(options.pulses) +
                       " pulses detected; the connection is unreliable or the signal is too quiet.";
    }
    return result;
}

} // namespace vs
