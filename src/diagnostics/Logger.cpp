#include "diagnostics/Logger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <ctime>
#include <system_error>

namespace vs {

namespace {
const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warning: return "WARN ";
    case LogLevel::Error: return "ERROR";
    default: return "?    ";
    }
}
} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

void Logger::open(const std::filesystem::path& file, std::size_t maxBytes) {
    std::lock_guard lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Simple rotation: keep one previous log.
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) > maxBytes) {
        std::filesystem::path old = file;
        old += ".1";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(file, old, ec);
    }
    path_ = file;
    file_ = _wfopen(file.c_str(), L"a");
}

void Logger::setLevel(LogLevel level) noexcept {
    std::lock_guard lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const noexcept {
    std::lock_guard lock(mutex_);
    return level_;
}

std::filesystem::path Logger::path() const {
    std::lock_guard lock(mutex_);
    return path_;
}

void Logger::write(LogLevel level, const std::string& message) {
    std::lock_guard lock(mutex_);
    if (level < level_ || level_ == LogLevel::Off) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
    localtime_s(&local, &t);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);

    char line[64];
    std::snprintf(line, sizeof(line), "%s.%03d [%s] ", stamp, static_cast<int>(ms), levelName(level));
    const std::string full = std::string(line) + message + "\n";
    OutputDebugStringA(full.c_str());
    if (file_ != nullptr) {
        std::fputs(full.c_str(), file_);
        std::fflush(file_);
    }
}

void logDebug(const std::string& m) { Logger::instance().write(LogLevel::Debug, m); }
void logInfo(const std::string& m) { Logger::instance().write(LogLevel::Info, m); }
void logWarning(const std::string& m) { Logger::instance().write(LogLevel::Warning, m); }
void logError(const std::string& m) { Logger::instance().write(LogLevel::Error, m); }

std::filesystem::path appDataDirectory() {
    PWSTR raw = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw != nullptr) {
        dir = std::filesystem::path(raw) / L"VoiceStudio";
    }
    if (raw != nullptr) {
        CoTaskMemFree(raw);
    }
    if (dir.empty()) {
        dir = std::filesystem::temp_directory_path() / L"VoiceStudio";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace vs
