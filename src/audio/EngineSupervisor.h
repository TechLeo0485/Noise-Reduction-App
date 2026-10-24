#pragma once

#include "audio/AudioEngine.h"
#include "audio/EngineConfig.h"
#include "core/Parameters.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace vs {

enum class EngineState { Stopped, Starting, Running, Degraded, Recovering, Error };

[[nodiscard]] const char* engineStateName(EngineState state) noexcept;

struct EngineStatus {
    EngineState state = EngineState::Stopped;
    std::string message;     // user-facing, actionable
    std::string lastError;   // technical detail
    std::uint64_t restartCount = 0;
    EngineRuntimeInfo info{};
};

// Control thread that owns the AudioEngine lifecycle.
//
// All blocking work (WASAPI initialisation, device re-opening, recovery back-off) happens
// here, never on the UI thread and never on the audio threads. The UI can freeze without
// affecting audio, and audio threads report failures only by setting atomic flags.
class EngineSupervisor {
public:
    explicit EngineSupervisor(EngineParameters& params);
    ~EngineSupervisor();
    EngineSupervisor(const EngineSupervisor&) = delete;
    EngineSupervisor& operator=(const EngineSupervisor&) = delete;

    // Called on the supervisor thread whenever the status changes. Set before launch().
    void setStatusListener(std::function<void()> listener);
    void launch();
    void shutdown();

    void applyConfig(const EngineConfig& config); // restarts streams if the config changed
    void setEnabled(bool enabled);
    void requestRestart();
    void notifyDevicesChanged();
    void notifyDefaultDeviceChanged(bool captureFlow);

    // Stops the engine, runs `task` on the supervisor thread, then restarts if enabled.
    // Used by diagnostics that need exclusive access to devices (e.g. virtual cable test).
    void runWithEngineStopped(std::function<void()> task);

    [[nodiscard]] EngineStatus status() const;
    [[nodiscard]] EngineConfig config() const;
    [[nodiscard]] AudioEngine& engine() noexcept { return engine_; }

private:
    using Clock = std::chrono::steady_clock;

    void threadMain();
    void post(std::function<void()> command);
    void attemptStart(IMMDeviceEnumerator* enumerator);
    void stopEngine(EngineState state, const std::string& message);
    void checkHealth();
    void scheduleRetry(const std::string& reason);
    void publish(EngineState state, const std::string& message, const std::string& error = {});

    EngineParameters& params_;
    AudioEngine engine_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> commands_;
    bool quit_ = false;
    EngineStatus status_{};
    EngineConfig config_{};
    std::function<void()> listener_;

    // Supervisor-thread-only state.
    bool enabled_ = false;
    bool startPending_ = false;
    Clock::time_point startAt_{};
    std::chrono::milliseconds backoff_{250};
    Clock::time_point runningSince_{};

    std::thread thread_;
};

} // namespace vs
