#include "app/CrashHandler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cwchar>
#include <system_error>

namespace vs {

namespace {

wchar_t g_dumpDirectory[MAX_PATH] = {};

LONG WINAPI writeMinidump(EXCEPTION_POINTERS* exception) {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t path[MAX_PATH] = {};
    std::swprintf(path, MAX_PATH, L"%s\\VoiceStudio_%04u%02u%02u_%02u%02u%02u.dmp", g_dumpDirectory, t.wYear, t.wMonth,
                  t.wDay, t.wHour, t.wMinute, t.wSecond);

    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashHandler(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    wcsncpy_s(g_dumpDirectory, directory.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(writeMinidump);
}

} // namespace vs
