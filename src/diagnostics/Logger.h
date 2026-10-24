#pragma once

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace vs {

enum class LogLevel : int { Debug = 0, Info = 1, Warning = 2, Error = 3, Off = 4 };

// Small thread-safe file logger for NON-real-time code only (control thread, UI, startup).
// Audio threads never log; they publish counters through EngineMetrics instead.
//
// Privacy: callers log events and device names only. Audio content is never logged.
class Logger {
public:
    static Logger& instance();

    void open(const std::filesystem::path& file, std::size_t maxBytes = 2 * 1024 * 1024);
    void setLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;
    [[nodiscard]] std::filesystem::path path() const;

    void write(LogLevel level, const std::string& message);

private:
    Logger() = default;
    ~Logger();

    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::filesystem::path path_;
    LogLevel level_ = LogLevel::Info;
};

void logDebug(const std::string& message);
void logInfo(const std::string& message);
void logWarning(const std::string& message);
void logError(const std::string& message);

// %LOCALAPPDATA%\VoiceStudio (created on demand).
[[nodiscard]] std::filesystem::path appDataDirectory();

} // namespace vs
