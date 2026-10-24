#pragma once

#include "audio/wasapi/WasapiCommon.h"

#include <thread>
#include <vector>

namespace vs {

struct RenderPathMetrics;

// Supplies mono audio at the render device's sample rate. Must be real-time safe.
class IRenderSource {
public:
    virtual ~IRenderSource() = default;
    virtual void renderMono(float* out, std::size_t frames) noexcept = 0;
};

// Event-driven shared-mode WASAPI render on a dedicated MMCSS thread.
//
// Keeps at most two device periods queued (the whole buffer in Safe mode) instead of always
// filling the entire endpoint buffer, which bounds render-side latency.
class WasapiRenderStream {
public:
    WasapiRenderStream() = default;
    ~WasapiRenderStream();
    WasapiRenderStream(const WasapiRenderStream&) = delete;
    WasapiRenderStream& operator=(const WasapiRenderStream&) = delete;

    HRESULT open(IMMDevice* device, BufferMode mode, double safeBufferMs, StreamRole role, std::string& error);
    HRESULT start(IRenderSource* source, StreamFailure* failure, RenderPathMetrics* metrics, std::string& error);
    void stop() noexcept;

    [[nodiscard]] const StreamInfo& info() const noexcept { return info_; }
    [[nodiscard]] bool isRunning() const noexcept { return thread_.joinable(); }
    [[nodiscard]] std::uint32_t maxQueuedFrames() const noexcept { return maxQueuedFrames_; }

private:
    void threadMain() noexcept;

    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> renderClient_;
    UniqueEvent audioEvent_;
    UniqueEvent stopEvent_;
    StreamInfo info_{};
    StreamRole role_ = StreamRole::Output;
    std::uint32_t maxQueuedFrames_ = 0;
    std::vector<float> mono_;
    IRenderSource* source_ = nullptr;
    StreamFailure* failure_ = nullptr;
    RenderPathMetrics* metrics_ = nullptr;
    std::thread thread_;
};

} // namespace vs
