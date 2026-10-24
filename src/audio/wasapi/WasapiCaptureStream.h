#pragma once

#include "audio/wasapi/WasapiCommon.h"

#include <thread>
#include <vector>

namespace vs {

// Receives captured audio on the capture thread. Implementations must be real-time safe.
class ICaptureSink {
public:
    virtual ~ICaptureSink() = default;
    // `mono` holds `frames` samples at the device sample rate (channels already averaged).
    // `discontinuity` is true when WASAPI reported a glitch before this packet.
    virtual void onCapturedAudio(const float* mono, std::size_t frames, bool discontinuity) noexcept = 0;
};

// Event-driven shared-mode WASAPI capture on a dedicated MMCSS thread.
class WasapiCaptureStream {
public:
    WasapiCaptureStream() = default;
    ~WasapiCaptureStream();
    WasapiCaptureStream(const WasapiCaptureStream&) = delete;
    WasapiCaptureStream& operator=(const WasapiCaptureStream&) = delete;

    // Non-real-time. Initialises the client and allocates conversion buffers.
    HRESULT open(IMMDevice* device, BufferMode mode, double safeBufferMs, std::string& error);
    // Starts the capture thread. `sink` and `failure` must outlive the stream.
    HRESULT start(ICaptureSink* sink, StreamFailure* failure, std::string& error);
    // Stops the thread and releases the client. Safe to call repeatedly.
    void stop() noexcept;

    [[nodiscard]] const StreamInfo& info() const noexcept { return info_; }
    [[nodiscard]] bool isRunning() const noexcept { return thread_.joinable(); }

private:
    void threadMain() noexcept;

    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> captureClient_;
    UniqueEvent audioEvent_;
    UniqueEvent stopEvent_;
    StreamInfo info_{};
    std::vector<float> mono_;
    ICaptureSink* sink_ = nullptr;
    StreamFailure* failure_ = nullptr;
    std::thread thread_;
};

} // namespace vs
