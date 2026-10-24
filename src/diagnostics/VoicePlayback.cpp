#include "diagnostics/VoicePlayback.h"

#include "audio/buffers/Resampler.h"
#include "audio/buffers/SampleConversion.h"
#include "audio/wasapi/WasapiCommon.h"
#include "devices/DeviceManager.h"

#include <algorithm>
#include <vector>

namespace vs {

bool playMonoBuffer(const std::wstring& deviceId, const std::vector<float>& samples48k, std::atomic<bool>& cancel,
                    std::string& deviceName, std::string& error) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = "Cannot access audio devices: " + hresultToString(hr);
        return false;
    }
    ComPtr<IMMDevice> device;
    hr = openEndpoint(enumerator.Get(), deviceId, eRender, device);
    if (FAILED(hr)) {
        error = "Playback device unavailable: " + hresultToString(hr);
        return false;
    }
    deviceName = toUtf8(deviceFriendlyName(device.Get()));

    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr)) {
        error = "Activate failed: " + hresultToString(hr);
        return false;
    }
    WAVEFORMATEX* raw = nullptr;
    if (FAILED(hr = client->GetMixFormat(&raw))) {
        error = "GetMixFormat failed: " + hresultToString(hr);
        return false;
    }
    UniqueWaveFormat mix(raw);
    const StreamFormat format = toStreamFormat(raw);
    if (!format.isSupported()) {
        error = "Unsupported playback format: " + format.describe();
        return false;
    }
    constexpr REFERENCE_TIME kBuffer = 1000000; // 100 ms
    if (FAILED(hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, kBuffer, 0, raw, nullptr))) {
        error = "Initialize failed: " + hresultToString(hr);
        return false;
    }
    UINT32 bufferFrames = 0;
    client->GetBufferSize(&bufferFrames);
    ComPtr<IAudioRenderClient> render;
    if (FAILED(hr = client->GetService(__uuidof(IAudioRenderClient), &render))) {
        error = "GetService failed: " + hresultToString(hr);
        return false;
    }

    // Convert the whole clip to the device rate up front (non-real-time; a few seconds of audio).
    std::vector<float> clip;
    if (format.sampleRate == 48000) {
        clip = samples48k;
    } else {
        Resampler rs;
        rs.configure(48000.0, format.sampleRate, 4800);
        std::vector<float> out(16384);
        for (std::size_t off = 0; off < samples48k.size(); off += 4800) {
            const std::size_t n = std::min<std::size_t>(4800, samples48k.size() - off);
            rs.write(samples48k.data() + off, n);
            const std::size_t got = rs.read(out.data(), out.size());
            clip.insert(clip.end(), out.begin(), out.begin() + static_cast<std::ptrdiff_t>(got));
        }
    }
    // Short fades so playback starts and ends without clicks.
    const std::size_t fade = std::min<std::size_t>(clip.size() / 2, format.sampleRate / 200);
    for (std::size_t i = 0; i < fade; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(fade);
        clip[i] *= g;
        clip[clip.size() - 1 - i] *= g;
    }

    if (FAILED(hr = client->Start())) {
        error = "Start failed: " + hresultToString(hr);
        return false;
    }
    std::vector<float> chunk(bufferFrames);
    std::size_t position = 0;
    bool ok = true;
    while (!cancel.load()) {
        UINT32 padding = 0;
        if (FAILED(hr = client->GetCurrentPadding(&padding))) {
            error = "Playback device stopped: " + hresultToString(hr);
            ok = false;
            break;
        }
        if (position >= clip.size()) {
            if (padding == 0) {
                break; // everything has been played
            }
        } else if (padding < bufferFrames) {
            const UINT32 frames = bufferFrames - padding;
            BYTE* data = nullptr;
            if (SUCCEEDED(render->GetBuffer(frames, &data))) {
                const std::size_t n = std::min<std::size_t>(frames, clip.size() - position);
                std::copy(clip.begin() + static_cast<std::ptrdiff_t>(position),
                          clip.begin() + static_cast<std::ptrdiff_t>(position + n), chunk.begin());
                std::fill(chunk.begin() + static_cast<std::ptrdiff_t>(n), chunk.begin() + frames, 0.0f);
                monoToInterleaved(chunk.data(), frames, format, data);
                render->ReleaseBuffer(frames, 0);
                position += n;
            }
        }
        Sleep(10);
    }
    client->Stop();
    return ok;
}

} // namespace vs
