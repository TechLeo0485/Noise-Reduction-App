#include "audio/EngineSupervisor.h"

#include "core/RealtimeThread.h"
#include "diagnostics/Logger.h"

#include <algorithm>

namespace vs {

namespace {
constexpr std::chrono::milliseconds kInitialBackoff{250};
constexpr std::chrono::milliseconds kMaxBackoff{5000};
constexpr std::chrono::milliseconds kDeviceChangeDebounce{400};
constexpr std::chrono::milliseconds kPollInterval{100};
constexpr double kCaptureStallMs = 2000.0;
} // namespace

const char* engineStateName(EngineState state) noexcept {
    switch (state) {
    case EngineState::Stopped: return "Stopped";
    case EngineState::Starting: return "Starting";
    case EngineState::Running: return "Running";
    case EngineState::Degraded: return "Running (no output)";
    case EngineState::Recovering: return "Recovering";
    case EngineState::Error: return "Error";
    }
    return "Unknown";
}

EngineSupervisor::EngineSupervisor(EngineParameters& params) : params_(params), engine_(params) {}

EngineSupervisor::~EngineSupervisor() {
    shutdown();
}

void EngineSupervisor::setStatusListener(std::function<void()> listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

void EngineSupervisor::launch() {
    if (!thread_.joinable()) {
        thread_ = std::thread([this] { threadMain(); });
    }
}

void EngineSupervisor::shutdown() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
        listener_ = nullptr; // the owner may be mid-destruction; stop notifying it
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    engine_.stop();
}

void EngineSupervisor::post(std::function<void()> command) {
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(command));
    }
    wake_.notify_all();
}

void EngineSupervisor::applyConfig(const EngineConfig& config) {
    post([this, config] {
        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            changed = !(config_ == config);
            config_ = config;
        }
        if (changed && enabled_) {
            logInfo("Configuration changed; restarting audio streams");
            startPending_ = true;
            startAt_ = Clock::now();
            backoff_ = kInitialBackoff;
        }
    });
}

void EngineSupervisor::setEnabled(bool enabled) {
    post([this, enabled] {
        enabled_ = enabled;
        if (enabled) {
            startPending_ = true;
            startAt_ = Clock::now();
            backoff_ = kInitialBackoff;
        } else {
            startPending_ = false;
            stopEngine(EngineState::Stopped, "Audio engine stopped");
        }
    });
}

void EngineSupervisor::requestRestart() {
    post([this] {
        if (enabled_) {
            startPending_ = true;
            startAt_ = Clock::now();
            backoff_ = kInitialBackoff;
        }
    });
}

void EngineSupervisor::notifyDevicesChanged() {
    post([this] {
        if (!enabled_) {
            return;
        }
        // A running engine on still-present devices is left alone: an actual loss of the active
        // device is reported by the stream itself. If we are waiting for a device, retry soon.
        const EngineState state = status().state;
        if (state == EngineState::Error || state == EngineState::Recovering || state == EngineState::Degraded) {
            startPending_ = true;
            startAt_ = Clock::now() + kDeviceChangeDebounce;
            backoff_ = kInitialBackoff;
        }
    });
}

void EngineSupervisor::notifyDefaultDeviceChanged(bool captureFlow) {
    post([this, captureFlow] {
        if (!enabled_) {
            return;
        }
        const EngineConfig cfg = config();
        const bool affected = captureFlow ? cfg.inputFollowDefault
                                          : (cfg.outputFollowDefault || (cfg.monitorEnabled && cfg.monitorDeviceId.empty()));
        if (affected) {
            logInfo(std::string("Default ") + (captureFlow ? "input" : "output") + " device changed; restarting");
            startPending_ = true;
            startAt_ = Clock::now() + kDeviceChangeDebounce;
            backoff_ = kInitialBackoff;
        }
    });
}

void EngineSupervisor::runWithEngineStopped(std::function<void()> task) {
    post([this, task = std::move(task)] {
        const bool wasRunning = engine_.isRunning();
        if (wasRunning) {
            engine_.stop();
            publish(EngineState::Stopped, "Paused for diagnostics");
        }
        task();
        if (enabled_) {
            startPending_ = true;
            startAt_ = Clock::now();
        }
    });
}

EngineStatus EngineSupervisor::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

EngineConfig EngineSupervisor::config() const {
    std::lock_guard lock(mutex_);
    return config_;
}

void EngineSupervisor::publish(EngineState state, const std::string& message, const std::string& error) {
    std::function<void()> listener;
    {
        std::lock_guard lock(mutex_);
        status_.state = state;
        status_.message = message;
        if (!error.empty()) {
            status_.lastError = error;
        }
        status_.info = engine_.runtimeInfo();
        listener = listener_;
    }
    if (listener) {
        listener();
    }
}

void EngineSupervisor::stopEngine(EngineState state, const std::string& message) {
    engine_.stop();
    publish(state, message);
}

void EngineSupervisor::scheduleRetry(const std::string& reason) {
    const EngineConfig cfg = config();
    engine_.stop();
    if (!cfg.autoRecover) {
        startPending_ = false;
        publish(EngineState::Error, reason + " - automatic recovery is disabled; press Restart.", reason);
        return;
    }
    startPending_ = true;
    startAt_ = Clock::now() + backoff_;
    logWarning(reason + "; retrying in " + std::to_string(backoff_.count()) + " ms");
    publish(EngineState::Recovering, reason + " - retrying automatically...", reason);
    backoff_ = std::min(backoff_ * 2, kMaxBackoff);
}

void EngineSupervisor::attemptStart(IMMDeviceEnumerator* enumerator) {
    startPending_ = false;
    if (enumerator == nullptr) {
        publish(EngineState::Error, "Windows audio device enumerator unavailable", "CoCreateInstance(MMDeviceEnumerator) failed");
        return;
    }
    publish(EngineState::Starting, "Starting audio engine...");
    const EngineConfig cfg = config();
    const AudioEngine::StartResult result = engine_.start(cfg, enumerator);
    if (!result.ok) {
        logError("Engine start failed: " + result.error);
        scheduleRetry(result.error);
        return;
    }

    {
        std::lock_guard lock(mutex_);
        ++status_.restartCount;
    }
    runningSince_ = Clock::now();
    const EngineRuntimeInfo& info = engine_.runtimeInfo();
    logInfo("Engine started. Input: " + info.inputName + " [" + info.capture.format.describe() + ", period " +
            std::to_string(info.capture.periodMs) + " ms" + (info.capture.usedAudioClient3 ? ", IAudioClient3" : "") + "]");
    if (info.outputRunning) {
        logInfo("Output: " + info.outputName + " [" + info.output.format.describe() + ", period " +
                std::to_string(info.output.periodMs) + " ms, target " + std::to_string(info.outputTargetMs) + " ms]");
        publish(EngineState::Running, "Processing " + info.inputName + " -> " + info.outputName);
    } else {
        logWarning("Output not running: " + info.outputError);
        publish(EngineState::Degraded, "Microphone active, but no processed output: " + info.outputError, info.outputError);
    }
}

void EngineSupervisor::checkHealth() {
    if (!engine_.isRunning()) {
        return;
    }
    StreamFailure& failure = engine_.failure();
    if (failure.failed.load()) {
        const auto hr = static_cast<HRESULT>(failure.hr.load());
        const auto role = static_cast<StreamRole>(failure.role.load());
        const char* what = role == StreamRole::Capture ? "Microphone" : role == StreamRole::Output ? "Output device" : "Monitor device";
        scheduleRetry(std::string(what) + " stopped: " + hresultToString(hr));
        return;
    }
    const std::int64_t last = engine_.metrics().lastCaptureTick.load(std::memory_order_relaxed);
    const double sinceMs = HighResClock::ticksToMilliseconds(HighResClock::now() - last);
    if (sinceMs > kCaptureStallMs) {
        scheduleRetry("Microphone stopped delivering audio (stalled for " + std::to_string(static_cast<int>(sinceMs)) + " ms)");
        return;
    }
    // A stable run resets the back-off.
    if (Clock::now() - runningSince_ > std::chrono::seconds(10)) {
        backoff_ = kInitialBackoff;
    }
}

void EngineSupervisor::threadMain() {
    setCurrentThreadName(L"VoiceStudio Supervisor");
    ComInitializer com(COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> enumerator;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        logError("CoCreateInstance(MMDeviceEnumerator) failed: " + hresultToString(hr));
    }

    for (;;) {
        std::deque<std::function<void()>> pending;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, kPollInterval, [this] { return quit_ || !commands_.empty(); });
            if (quit_) {
                break;
            }
            pending.swap(commands_);
        }
        for (auto& command : pending) {
            command();
        }
        checkHealth();
        if (enabled_ && startPending_ && Clock::now() >= startAt_) {
            attemptStart(enumerator.Get());
        }
    }
    engine_.stop();
    publish(EngineState::Stopped, "Audio engine stopped");
}

} // namespace vs
