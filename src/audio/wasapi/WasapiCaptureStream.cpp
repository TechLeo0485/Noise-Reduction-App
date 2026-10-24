#include "audio/wasapi/WasapiCaptureStream.h"

#include "core/RealtimeThread.h"

#include <algorithm>

namespace vs {

WasapiCaptureStream::~WasapiCaptureStream() {
    stop();
}

HRESULT WasapiCaptureStream::open(IMMDevice* device, BufferMode mode, double safeBufferMs, std::string& error) {
    stop();
    audioEvent_ = UniqueEvent(false);
    stopEvent_ = UniqueEvent(true);
    if (!audioEvent_ || !stopEvent_) {
        error = "CreateEvent failed";
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT hr = initializeSharedClient(device, mode, safeBufferMs, audioEvent_.get(), client_, info_, error);
    if (FAILED(hr)) {
        return hr;
    }
    hr = client_->GetService(__uuidof(IAudioCaptureClient), &captureClient_);
    if (FAILED(hr)) {
        error = "GetService(IAudioCaptureClient) failed: " + hresultToString(hr);
        client_.Reset();
        return hr;
    }
    // A packet never exceeds the endpoint buffer; allocate generously once.
    mono_.assign(static_cast<std::size_t>(info_.bufferFrames) * 2 + 64, 0.0f);
    return S_OK;
}

HRESULT WasapiCaptureStream::start(ICaptureSink* sink, StreamFailure* failure, std::string& error) {
    if (!client_ || thread_.joinable()) {
        error = "Capture stream not open";
        return E_UNEXPECTED;
    }
    sink_ = sink;
    failure_ = failure;
    ResetEvent(stopEvent_.get());
    const HRESULT hr = client_->Start();
    if (FAILED(hr)) {
        error = "Capture Start failed: " + hresultToString(hr);
        return hr;
    }
    thread_ = std::thread([this] { threadMain(); });
    return S_OK;
}

void WasapiCaptureStream::stop() noexcept {
    if (thread_.joinable()) {
        SetEvent(stopEvent_.get());
        thread_.join();
    }
    if (client_) {
        client_->Stop();
    }
    captureClient_.Reset();
    client_.Reset();
}

void WasapiCaptureStream::threadMain() noexcept {
    ComInitializer com(COINIT_MULTITHREADED);
    setCurrentThreadName(L"VoiceStudio Capture");
    ScopedMmcss mmcss(L"Pro Audio");
    ScopedDenormalSuppression denormals;

    const HANDLE handles[2] = {stopEvent_.get(), audioEvent_.get()};
    const std::size_t capacity = mono_.size();

    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 200);
        if (wait == WAIT_OBJECT_0) {
            break; // stop requested
        }
        if (wait == WAIT_TIMEOUT) {
            continue; // stall detection is done by the supervisor
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            failure_->report(StreamRole::Capture, HRESULT_FROM_WIN32(GetLastError()));
            break;
        }

        UINT32 packetFrames = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packetFrames);
        while (SUCCEEDED(hr) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) {
                hr = S_OK;
                break;
            }
            if (FAILED(hr)) {
                break;
            }
            const std::size_t usable = std::min<std::size_t>(frames, capacity);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
                std::fill(mono_.begin(), mono_.begin() + static_cast<std::ptrdiff_t>(usable), 0.0f);
            } else {
                interleavedToMono(data, usable, info_.format, mono_.data());
            }
            // Release the device buffer before processing to give the driver its memory back early.
            hr = captureClient_->ReleaseBuffer(frames);
            if (FAILED(hr)) {
                break;
            }
            sink_->onCapturedAudio(mono_.data(), usable, (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
            hr = captureClient_->GetNextPacketSize(&packetFrames);
        }
        if (FAILED(hr)) {
            failure_->report(StreamRole::Capture, hr);
            break;
        }
    }
}

} // namespace vs
