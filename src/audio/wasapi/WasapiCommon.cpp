#include "audio/wasapi/WasapiCommon.h"

#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <cstdio>

namespace vs {

ComInitializer::ComInitializer(DWORD flags) noexcept : hr_(CoInitializeEx(nullptr, flags)) {}

ComInitializer::~ComInitializer() {
    if (SUCCEEDED(hr_)) {
        CoUninitialize();
    }
}

UniqueEvent::UniqueEvent(bool manualReset) noexcept
    : handle_(CreateEventW(nullptr, manualReset ? TRUE : FALSE, FALSE, nullptr)) {}

UniqueEvent::~UniqueEvent() {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
    }
}

UniqueEvent& UniqueEvent::operator=(UniqueEvent&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
        handle_ = other.release();
    }
    return *this;
}

std::string hresultToString(HRESULT hr) {
    struct KnownError {
        HRESULT code;
        const char* text;
    };
    const KnownError knownErrors[] = {
        {AUDCLNT_E_DEVICE_INVALIDATED, "Audio device was removed or disabled"},
        {AUDCLNT_E_DEVICE_IN_USE, "Audio device is in exclusive use by another application"},
        {AUDCLNT_E_UNSUPPORTED_FORMAT, "Audio format is not supported by the device"},
        {AUDCLNT_E_SERVICE_NOT_RUNNING, "Windows Audio service is not running"},
        {AUDCLNT_E_ENDPOINT_CREATE_FAILED, "Audio endpoint could not be created"},
        {AUDCLNT_E_RESOURCES_INVALIDATED, "Audio resources were invalidated (e.g. system suspend)"},
        {AUDCLNT_E_BUFFER_SIZE_ERROR, "Requested audio buffer size is not supported"},
        {E_NOTFOUND, "Audio device not found or not active"},
        {E_ACCESSDENIED, "Microphone access denied (check Windows privacy settings)"},
    };
    const char* known = nullptr;
    for (const auto& entry : knownErrors) {
        if (entry.code == hr) {
            known = entry.text;
            break;
        }
    }
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
    if (known != nullptr) {
        return std::string(known) + " (" + code + ")";
    }
    char* buffer = nullptr;
    const DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = len > 0 && buffer != nullptr ? std::string(buffer, len) : std::string("Unknown error");
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
        message.pop_back();
    }
    return message + " (" + code + ")";
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

StreamFormat toStreamFormat(const WAVEFORMATEX* wfx) noexcept {
    StreamFormat f;
    if (wfx == nullptr) {
        return f;
    }
    f.sampleRate = wfx->nSamplesPerSec;
    f.channels = wfx->nChannels;
    f.bitsPerSample = wfx->wBitsPerSample;
    f.validBits = wfx->wBitsPerSample;
    f.blockAlign = wfx->nBlockAlign;

    bool isFloat = false;
    bool isPcm = false;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        f.validBits = ext->Samples.wValidBitsPerSample != 0 ? ext->Samples.wValidBitsPerSample : wfx->wBitsPerSample;
        f.channelMask = ext->dwChannelMask;
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
        isPcm = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != FALSE;
    } else {
        isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        isPcm = wfx->wFormatTag == WAVE_FORMAT_PCM;
    }

    if (isFloat && f.bitsPerSample == 32) {
        f.sampleType = SampleType::Float32;
    } else if (isPcm && f.bitsPerSample == 16) {
        f.sampleType = SampleType::Int16;
    } else if (isPcm && f.bitsPerSample == 24) {
        f.sampleType = SampleType::Int24Packed;
    } else if (isPcm && f.bitsPerSample == 32) {
        f.sampleType = SampleType::Int32;
    }
    return f;
}

HRESULT openEndpoint(IMMDeviceEnumerator* enumerator, const std::wstring& id, EDataFlow flow,
                     ComPtr<IMMDevice>& deviceOut) {
    deviceOut.Reset();
    HRESULT hr = id.empty() ? enumerator->GetDefaultAudioEndpoint(flow, eConsole, &deviceOut)
                            : enumerator->GetDevice(id.c_str(), &deviceOut);
    if (FAILED(hr)) {
        return hr;
    }
    DWORD state = 0;
    hr = deviceOut->GetState(&state);
    if (FAILED(hr)) {
        return hr;
    }
    if (state != DEVICE_STATE_ACTIVE) {
        deviceOut.Reset();
        return E_NOTFOUND;
    }
    return S_OK;
}

namespace {

double framesToMs(std::uint32_t frames, std::uint32_t rate) {
    return rate > 0 ? static_cast<double>(frames) * 1000.0 / rate : 0.0;
}

HRESULT tryAudioClient3(IMMDevice* device, const WAVEFORMATEX* format, ComPtr<IAudioClient>& clientOut,
                        std::uint32_t& periodFramesOut) {
    ComPtr<IAudioClient3> client3;
    HRESULT hr = device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, &client3);
    if (FAILED(hr)) {
        return hr;
    }
    UINT32 defaultPeriod = 0, fundamental = 0, minPeriod = 0, maxPeriod = 0;
    hr = client3->GetSharedModeEnginePeriod(format, &defaultPeriod, &fundamental, &minPeriod, &maxPeriod);
    if (FAILED(hr)) {
        return hr;
    }
    hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, format, nullptr);
    if (FAILED(hr)) {
        return hr;
    }
    periodFramesOut = minPeriod;
    return client3.As(&clientOut);
}

} // namespace

HRESULT initializeSharedClient(IMMDevice* device, BufferMode mode, double safeBufferMs, HANDLE eventHandle,
                               ComPtr<IAudioClient>& clientOut, StreamInfo& info, std::string& error) {
    clientOut.Reset();
    info = StreamInfo{};

    // Query the shared-mode mix format with a temporary client.
    ComPtr<IAudioClient> probe;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &probe);
    if (FAILED(hr)) {
        error = "Activate IAudioClient failed: " + hresultToString(hr);
        return hr;
    }
    WAVEFORMATEX* rawFormat = nullptr;
    hr = probe->GetMixFormat(&rawFormat);
    if (FAILED(hr)) {
        error = "GetMixFormat failed: " + hresultToString(hr);
        return hr;
    }
    UniqueWaveFormat mixFormat(rawFormat);
    info.format = toStreamFormat(mixFormat.get());
    if (!info.format.isSupported()) {
        error = "Unsupported device mix format (" + info.format.describe() + ")";
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    std::uint32_t periodFrames = 0;
    if (mode == BufferMode::LowLatency) {
        ComPtr<IAudioClient> client;
        if (SUCCEEDED(tryAudioClient3(device, mixFormat.get(), client, periodFrames))) {
            clientOut = client;
            info.usedAudioClient3 = true;
        }
    }

    if (!clientOut) {
        // A client whose Initialize failed cannot be reused, so always activate a fresh one.
        ComPtr<IAudioClient> client;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        if (FAILED(hr)) {
            error = "Activate IAudioClient failed: " + hresultToString(hr);
            return hr;
        }
        const REFERENCE_TIME requested =
            mode == BufferMode::Safe ? static_cast<REFERENCE_TIME>(safeBufferMs * 10000.0) : 0;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                                requested, 0, mixFormat.get(), nullptr);
        if (FAILED(hr)) {
            error = "IAudioClient::Initialize failed: " + hresultToString(hr);
            return hr;
        }
        REFERENCE_TIME defaultPeriod = 0, minimumPeriod = 0;
        if (SUCCEEDED(client->GetDevicePeriod(&defaultPeriod, &minimumPeriod))) {
            periodFrames = static_cast<std::uint32_t>(defaultPeriod * info.format.sampleRate / 10000000LL);
        }
        clientOut = client;
    }

    hr = clientOut->SetEventHandle(eventHandle);
    if (FAILED(hr)) {
        error = "SetEventHandle failed: " + hresultToString(hr);
        clientOut.Reset();
        return hr;
    }
    UINT32 bufferFrames = 0;
    hr = clientOut->GetBufferSize(&bufferFrames);
    if (FAILED(hr)) {
        error = "GetBufferSize failed: " + hresultToString(hr);
        clientOut.Reset();
        return hr;
    }
    REFERENCE_TIME latency = 0;
    if (SUCCEEDED(clientOut->GetStreamLatency(&latency))) {
        info.streamLatencyMs = static_cast<double>(latency) / 10000.0;
    }
    info.bufferFrames = bufferFrames;
    info.periodFrames = periodFrames > 0 ? periodFrames : bufferFrames / 2;
    info.bufferMs = framesToMs(info.bufferFrames, info.format.sampleRate);
    info.periodMs = framesToMs(info.periodFrames, info.format.sampleRate);
    return S_OK;
}

bool isDeviceLostError(HRESULT hr) noexcept {
    return hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
           hr == AUDCLNT_E_SERVICE_NOT_RUNNING || hr == E_NOTFOUND;
}

} // namespace vs
