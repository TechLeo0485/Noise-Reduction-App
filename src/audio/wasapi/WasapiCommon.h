#pragma once

#include "audio/buffers/SampleConversion.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace vs {

using Microsoft::WRL::ComPtr;

// RAII COM apartment initialisation for the current thread.
class ComInitializer {
public:
    explicit ComInitializer(DWORD flags = COINIT_MULTITHREADED) noexcept;
    ~ComInitializer();
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_ = E_FAIL;
};

// RAII Win32 event handle.
class UniqueEvent {
public:
    UniqueEvent() noexcept = default;
    explicit UniqueEvent(bool manualReset) noexcept;
    ~UniqueEvent();
    UniqueEvent(UniqueEvent&& other) noexcept : handle_(other.release()) {}
    UniqueEvent& operator=(UniqueEvent&& other) noexcept;
    UniqueEvent(const UniqueEvent&) = delete;
    UniqueEvent& operator=(const UniqueEvent&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
    HANDLE release() noexcept {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    HANDLE handle_ = nullptr;
};

struct CoTaskMemDeleter {
    void operator()(void* p) const noexcept { CoTaskMemFree(p); }
};
using UniqueWaveFormat = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

enum class BufferMode : int {
    LowLatency = 0, // IAudioClient3 minimum engine period where supported
    Default = 1,    // engine default period (typically 10 ms)
    Safe = 2,       // larger device buffer for unstable drivers / Bluetooth
};

enum class StreamRole : int { Capture = 0, Output = 1, Monitor = 2 };

// Set by a stream thread when the device fails (e.g. AUDCLNT_E_DEVICE_INVALIDATED).
// Polled by the supervisor; never blocks the audio thread.
struct StreamFailure {
    std::atomic<bool> failed{false};
    std::atomic<long> hr{S_OK};
    std::atomic<int> role{0};

    void report(StreamRole r, HRESULT code) noexcept {
        bool expected = false;
        if (failed.compare_exchange_strong(expected, true)) {
            hr.store(code);
            role.store(static_cast<int>(r));
        }
    }
    void clear() noexcept {
        failed.store(false);
        hr.store(S_OK);
    }
};

struct StreamInfo {
    StreamFormat format{};
    std::uint32_t bufferFrames = 0;
    std::uint32_t periodFrames = 0;
    double periodMs = 0.0;
    double bufferMs = 0.0;
    double streamLatencyMs = 0.0; // IAudioClient::GetStreamLatency
    bool usedAudioClient3 = false;
};

[[nodiscard]] std::string hresultToString(HRESULT hr);
[[nodiscard]] std::string toUtf8(const std::wstring& wide);
[[nodiscard]] std::wstring fromUtf8(const std::string& utf8);

[[nodiscard]] StreamFormat toStreamFormat(const WAVEFORMATEX* wfx) noexcept;

// Opens an endpoint by ID, or the default endpoint for `flow` when `id` is empty.
// Fails if the endpoint is not active.
HRESULT openEndpoint(IMMDeviceEnumerator* enumerator, const std::wstring& id, EDataFlow flow,
                     ComPtr<IMMDevice>& deviceOut);

// Activates and initialises an event-driven shared-mode client on the device's mix format.
// LowLatency tries IAudioClient3::InitializeSharedAudioStream with the minimum period and
// falls back to IAudioClient::Initialize. `eventHandle` is registered with SetEventHandle.
HRESULT initializeSharedClient(IMMDevice* device, BufferMode mode, double safeBufferMs, HANDLE eventHandle,
                               ComPtr<IAudioClient>& clientOut, StreamInfo& infoOut, std::string& errorOut);

[[nodiscard]] bool isDeviceLostError(HRESULT hr) noexcept;

} // namespace vs
