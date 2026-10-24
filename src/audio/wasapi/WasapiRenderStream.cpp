#include "audio/wasapi/WasapiRenderStream.h"

#include "audio/monitoring/EngineMetrics.h"
#include "core/RealtimeThread.h"

#include <algorithm>

namespace vs {

WasapiRenderStream::~WasapiRenderStream() {
    stop();
}

HRESULT WasapiRenderStream::open(IMMDevice* device, BufferMode mode, double safeBufferMs, StreamRole role,
                                 std::string& error) {
    stop();
    role_ = role;
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
    hr = client_->GetService(__uuidof(IAudioRenderClient), &renderClient_);
    if (FAILED(hr)) {
        error = "GetService(IAudioRenderClient) failed: " + hresultToString(hr);
        client_.Reset();
        return hr;
    }
    maxQueuedFrames_ = mode == BufferMode::Safe ? info_.bufferFrames
                                                : std::min(info_.bufferFrames, std::max(info_.periodFrames * 2, 1u));
    mono_.assign(info_.bufferFrames, 0.0f);
    return S_OK;
}

HRESULT WasapiRenderStream::start(IRenderSource* source, StreamFailure* failure, RenderPathMetrics* metrics,
                                  std::string& error) {
    if (!client_ || thread_.joinable()) {
        error = "Render stream not open";
        return E_UNEXPECTED;
    }
    source_ = source;
    failure_ = failure;
    metrics_ = metrics;
    ResetEvent(stopEvent_.get());

    // Pre-roll one period of silence so the first device pass does not glitch.
    BYTE* data = nullptr;
    const UINT32 preroll = std::min(info_.periodFrames, info_.bufferFrames);
    if (preroll > 0 && SUCCEEDED(renderClient_->GetBuffer(preroll, &data))) {
        renderClient_->ReleaseBuffer(preroll, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    const HRESULT hr = client_->Start();
    if (FAILED(hr)) {
        error = "Render Start failed: " + hresultToString(hr);
        return hr;
    }
    thread_ = std::thread([this] { threadMain(); });
    return S_OK;
}

void WasapiRenderStream::stop() noexcept {
    if (thread_.joinable()) {
        SetEvent(stopEvent_.get());
        thread_.join();
    }
    if (client_) {
        client_->Stop();
    }
    renderClient_.Reset();
    client_.Reset();
}

void WasapiRenderStream::threadMain() noexcept {
    ComInitializer com(COINIT_MULTITHREADED);
    setCurrentThreadName(role_ == StreamRole::Monitor ? L"VoiceStudio Monitor" : L"VoiceStudio Render");
    ScopedMmcss mmcss(L"Pro Audio");
    ScopedDenormalSuppression denormals;

    const HANDLE handles[2] = {stopEvent_.get(), audioEvent_.get()};
    const double msPerFrame = 1000.0 / static_cast<double>(info_.format.sampleRate);

    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 200);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_TIMEOUT) {
            continue;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            failure_->report(role_, HRESULT_FROM_WIN32(GetLastError()));
            break;
        }

        UINT32 padding = 0;
        HRESULT hr = client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            failure_->report(role_, hr);
            break;
        }
        metrics_->devicePaddingMs.store(static_cast<float>(padding * msPerFrame), std::memory_order_relaxed);
        if (padding >= maxQueuedFrames_) {
            continue;
        }
        const UINT32 frames = std::min<UINT32>(maxQueuedFrames_ - padding, static_cast<UINT32>(mono_.size()));

        BYTE* data = nullptr;
        hr = renderClient_->GetBuffer(frames, &data);
        if (FAILED(hr)) {
            failure_->report(role_, hr);
            break;
        }
        source_->renderMono(mono_.data(), frames);
        monoToInterleaved(mono_.data(), frames, info_.format, data);
        hr = renderClient_->ReleaseBuffer(frames, 0);
        if (FAILED(hr)) {
            failure_->report(role_, hr);
            break;
        }
    }
}

} // namespace vs
