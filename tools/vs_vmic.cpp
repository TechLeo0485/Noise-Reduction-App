// vs_vmic - installs and removes the VoiceStudio Virtual Microphone driver (driver/, test-signed).
//
//   vs_vmic status                 prints the device state, whether Windows Test Mode and Secure Boot are on
//   vs_vmic install <path\VsVirtualMic.inf>
//                                  creates the root-enumerated device Root\VoiceStudioVirtualMic (once) and
//                                  installs or updates its driver from the package next to the INF
//   vs_vmic uninstall              removes every such device and deletes the driver package from the store
//   vs_vmic finish                 guided setup with message boxes: if Secure Boot is on, restarts into the firmware
//                                  settings (where only the user can turn it off); then turns on Test Mode and
//                                  restarts; resumes by itself after each restart and confirms when the
//                                  microphone works. Asks before every change and changes nothing if the user says no.
//
// Runs elevated (requireAdministrator manifest). Secure Boot itself cannot be changed from Windows.
//
// Exit codes: 0 ok, 1 error, 2 usage, 3 ok but a restart is needed.

#include "devices/VirtualMicDriver.h"

#include <windows.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <newdev.h>
#include <setupapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kHardwareId[] = L"Root\\VoiceStudioVirtualMic";

void printError(const wchar_t* what) {
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::fwprintf(stderr, L"%ls failed (0x%08lX): %ls", what, code, text != nullptr ? text : L"\n");
    LocalFree(text);
}

bool hasOurHardwareId(HDEVINFO set, SP_DEVINFO_DATA& data) {
    wchar_t buffer[1024] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(buffer),
                                           sizeof(buffer) - sizeof(wchar_t) * 2, nullptr)) {
        return false;
    }
    for (const wchar_t* p = buffer; *p != L'\0'; p += wcslen(p) + 1) {
        if (_wcsicmp(p, kHardwareId) == 0) {
            return true;
        }
    }
    return false;
}

// Calls `fn` for every device (present or not) with our hardware ID. Returns how many there are.
template <typename Fn>
int forEachDevice(Fn fn) {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, 0);
    if (set == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int count = 0;
    SP_DEVINFO_DATA data{sizeof(data)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        if (hasOurHardwareId(set, data)) {
            ++count;
            fn(set, data);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return count;
}

int status() {
    const vs::VirtualMicStatus st = vs::queryVirtualMicDriver();
    std::wprintf(L"test-mode: %ls\n", st.testMode ? L"on" : L"off");
    std::wprintf(L"secure-boot: %ls\n", st.secureBoot ? L"on" : L"off");
    switch (st.state) {
    case vs::VirtualMicState::NotInstalled: std::wprintf(L"device: not installed\n"); break;
    case vs::VirtualMicState::Running: std::wprintf(L"device: running\n"); break;
    case vs::VirtualMicState::NotStarted: std::wprintf(L"device: installed, not started (restart?)\n"); break;
    case vs::VirtualMicState::Blocked:
        std::wprintf(L"device: problem 52 (driver signature not accepted - is Test Mode on?)\n");
        break;
    case vs::VirtualMicState::Problem: std::wprintf(L"device: problem %lu\n", st.problemCode); break;
    }
    return 0;
}

int install(const std::wstring& inf) {
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(inf.c_str(), MAX_PATH, full, nullptr) == 0 || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"INF not found: %ls\n", inf.c_str());
        return 1;
    }
    if (forEachDevice([](HDEVINFO, SP_DEVINFO_DATA&) {}) == 0) {
        // Create the device node; PnP then needs a driver for it, which UpdateDriverForPlugAndPlayDevices installs.
        GUID classGuid{};
        wchar_t className[MAX_CLASS_NAME_LEN] = {};
        if (!SetupDiGetINFClassW(full, &classGuid, className, MAX_CLASS_NAME_LEN, nullptr)) {
            printError(L"SetupDiGetINFClass");
            return 1;
        }
        HDEVINFO set = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
        if (set == INVALID_HANDLE_VALUE) {
            printError(L"SetupDiCreateDeviceInfoList");
            return 1;
        }
        SP_DEVINFO_DATA data{sizeof(data)};
        wchar_t ids[64] = {};
        wcscpy_s(ids, kHardwareId); // REG_MULTI_SZ: the array is zero-filled, so the double terminator is there
        const DWORD idsBytes = static_cast<DWORD>((wcslen(kHardwareId) + 2) * sizeof(wchar_t));
        const bool ok = SetupDiCreateDeviceInfoW(set, className, &classGuid, nullptr, nullptr, DICD_GENERATE_ID, &data) &&
                        SetupDiSetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, reinterpret_cast<BYTE*>(ids),
                                                          idsBytes) &&
                        SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &data);
        if (!ok) {
            printError(L"Creating the device");
        }
        SetupDiDestroyDeviceInfoList(set);
        if (!ok) {
            return 1;
        }
    }
    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHardwareId, full, INSTALLFLAG_FORCE, &reboot)) {
        printError(L"Installing the driver");
        return 1;
    }
    std::wprintf(L"installed%ls\n", reboot ? L" (restart needed)" : L"");
    return reboot ? 3 : 0;
}

int uninstall() {
    bool reboot = false;
    forEachDevice([&](HDEVINFO set, SP_DEVINFO_DATA& data) {
        BOOL needReboot = FALSE;
        if (!DiUninstallDevice(nullptr, set, &data, 0, &needReboot)) {
            printError(L"Removing the device");
        }
        reboot = reboot || needReboot;
    });
    // Delete our package(s) from the driver store: the published oem*.inf files that install our hardware ID.
    wchar_t windir[MAX_PATH] = {};
    GetWindowsDirectoryW(windir, MAX_PATH);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(windir) / L"INF", ec)) {
        const std::wstring name = entry.path().filename().wstring();
        if (_wcsnicmp(name.c_str(), L"oem", 3) != 0 || _wcsicmp(entry.path().extension().c_str(), L".inf") != 0) {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // INFs are UTF-16 or ANSI: look for the hardware ID in both.
        const std::string ansi = "Root\\VoiceStudioVirtualMic";
        std::string wide;
        for (char c : ansi) {
            wide += c;
            wide += '\0';
        }
        if (bytes.find(ansi) == std::string::npos && bytes.find(wide) == std::string::npos) {
            continue;
        }
        if (SetupUninstallOEMInfW(name.c_str(), SUOI_FORCEDELETE, nullptr)) {
            std::wprintf(L"removed driver package %ls\n", name.c_str());
        } else {
            printError(L"Removing the driver package");
        }
    }
    std::wprintf(L"uninstalled%ls\n", reboot ? L" (restart needed)" : L"");
    return reboot ? 3 : 0;
}

// ---- finish: the guided path to a working microphone (asks before every change) ----------------------------

constexpr wchar_t kTitle[] = L"VoiceStudio Virtual Microphone";
constexpr wchar_t kResumeValue[] = L"VoiceStudioVirtualMic";
constexpr wchar_t kSetupKey[] = L"Software\\VoiceStudio\\VirtualMicrophone";

std::filesystem::path exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

// Runs a command line hidden and returns its exit code (-1 if it could not start).
int runHidden(std::wstring commandLine) {
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

std::wstring systemTool(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, MAX_PATH);
    return L"\"" + (std::filesystem::path(dir) / name).wstring() + L"\"";
}

bool ask(const std::wstring& text) {
    return MessageBoxW(nullptr, text.c_str(), kTitle, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES;
}

void tell(const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | icon | MB_SETFOREGROUND | MB_TOPMOST);
}

// Runs "vs_vmic finish" once at the next sign-in of an administrator. Machine-wide RunOnce entries run elevated;
// per-user ones cannot start programs that need administrator rights.
void resumeAfterRestart() {
    const std::wstring command = L"\"" + (exeDirectory() / L"vs_vmic.exe").wstring() + L"\" finish";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, kResumeValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

// BitLocker would ask for the recovery key after a boot-configuration change: pause it for the next two
// restarts (Windows turns it back on by itself). Harmless where BitLocker is off.
void pauseBitLocker() {
    wchar_t drive[MAX_PATH] = L"C:";
    GetEnvironmentVariableW(L"SystemDrive", drive, MAX_PATH);
    runHidden(systemTool(L"manage-bde.exe") + L" -protectors -disable " + drive + L" -RebootCount 2");
}

void restartNow(bool toFirmware) {
    const std::wstring args = toFirmware ? L" /r /fw /t 5" : L" /r /t 5";
    if (runHidden(systemTool(L"shutdown.exe") + args) != 0 && toFirmware) {
        tell(L"Windows could not restart into the firmware settings by itself on this PC.\n\n"
             L"Restart Windows yourself and press the firmware key while the PC starts - often F2, F10, F12, Del or "
             L"Esc (the start screen usually shows it). VoiceStudio continues after you sign in again.",
             MB_ICONWARNING);
    }
}

const wchar_t kFirmwareSteps[] =
    L"In the firmware (UEFI/BIOS) settings that open after the restart:\n"
    L"  1. Find \"Secure Boot\" - usually under Security, Boot or Authentication.\n"
    L"  2. Set it to Disabled. (If it cannot be changed: set \"OS Type\" to \"Other OS\", or set a supervisor "
    L"password first.)\n"
    L"  3. Save and exit - often F10 - and let Windows start.\n\n"
    L"VoiceStudio continues automatically after you sign in.";

int finish() {
    if (HWND console = GetConsoleWindow(); console != nullptr) {
        ShowWindow(console, SW_HIDE);
    }
    const std::filesystem::path dir = exeDirectory();
    vs::VirtualMicStatus st = vs::queryVirtualMicDriver();

    if (st.state == vs::VirtualMicState::Running) {
        tell(L"The VoiceStudio Virtual Microphone is working.\n\n"
             L"In Discord, OBS, games or Windows choose \"VoiceStudio Virtual Microphone\" as the microphone. "
             L"VoiceStudio sends your voice to it automatically.");
        return 0;
    }
    if (st.state == vs::VirtualMicState::NotInstalled) {
        install((dir / L"vsvirtualmic.inf").wstring());
        st = vs::queryVirtualMicDriver();
    }

    if (st.secureBoot) {
        if (!ask(L"The VoiceStudio Virtual Microphone is a free, test-signed driver. Windows only runs it in Test Mode, "
                 L"and Test Mode needs Secure Boot turned off - it is on on this PC.\n\n"
                 L"No program can turn Secure Boot off: you change it in your PC's firmware settings. VoiceStudio can "
                 L"restart your PC straight into those settings now, and pause BitLocker for the next two restarts so "
                 L"you are not asked for the recovery key.\n\n"
                 L"Turning Secure Boot off lowers your PC's protection against boot-level malware. Some games' "
                 L"anti-cheat software also refuses to run without Secure Boot or in Test Mode.\n\n"
                 L"Save your work. Restart into the firmware settings now?")) {
            tell(L"Nothing was changed. You can finish later from the Start menu: \"Finish VoiceStudio Virtual "
                 L"Microphone setup\". Until then VoiceStudio can use VB-CABLE.");
            return 0;
        }
        tell(kFirmwareSteps);
        resumeAfterRestart();
        pauseBitLocker();
        restartNow(true);
        return 0;
    }

    if (!st.testMode) {
        if (!ask(L"Secure Boot is off. The last step is Windows Test Mode, which lets Windows run the test-signed "
                 L"VoiceStudio Virtual Microphone driver.\n\n"
                 L"This changes a Windows security setting:\n"
                 L"  - Windows also loads test-signed drivers (not only drivers verified by Microsoft).\n"
                 L"  - \"Test Mode\" is shown in the corner of the desktop.\n"
                 L"  - Some games' anti-cheat software refuses to run in Test Mode.\n"
                 L"  - BitLocker is paused for the next two restarts so you are not asked for the recovery key.\n"
                 L"Uninstalling VoiceStudio offers to turn Test Mode off again.\n\n"
                 L"Turn on Test Mode and restart now?")) {
            tell(L"Nothing was changed. You can finish later from the Start menu: \"Finish VoiceStudio Virtual "
                 L"Microphone setup\".");
            return 0;
        }
        pauseBitLocker();
        if (runHidden(systemTool(L"bcdedit.exe") + L" /set testsigning on") != 0) {
            tell(L"Windows did not allow turning on Test Mode (bcdedit failed). If Secure Boot was just turned off, "
                 L"restart once and try again from the Start menu.",
                 MB_ICONERROR);
            return 1;
        }
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kSetupKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
            ERROR_SUCCESS) {
            const DWORD one = 1;
            RegSetValueExW(key, L"TestModeEnabledBySetup", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
        resumeAfterRestart(); // confirms that the microphone works after the restart
        restartNow(false);
        return 0;
    }

    if (st.state == vs::VirtualMicState::Blocked) {
        // Test Mode is on, but the certificate is not trusted (e.g. it was removed): trust it and reinstall.
        const std::wstring cer = L"\"" + (dir / L"vsvirtualmic-test.cer").wstring() + L"\"";
        runHidden(systemTool(L"certutil.exe") + L" -f -addstore Root " + cer);
        runHidden(systemTool(L"certutil.exe") + L" -f -addstore TrustedPublisher " + cer);
        install((dir / L"vsvirtualmic.inf").wstring());
        st = vs::queryVirtualMicDriver();
        if (st.state == vs::VirtualMicState::Running) {
            return finish();
        }
    }
    if (ask(L"Test Mode is on, but the VoiceStudio Virtual Microphone has not started yet. A restart usually "
            L"finishes it.\n\nRestart now?")) {
        resumeAfterRestart();
        restartNow(false);
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && _wcsicmp(argv[1], L"status") == 0) {
        return status();
    }
    if (argc >= 3 && _wcsicmp(argv[1], L"install") == 0) {
        return install(argv[2]);
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"uninstall") == 0) {
        return uninstall();
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"finish") == 0) {
        return finish();
    }
    std::fwprintf(stderr, L"usage: vs_vmic status | install <VsVirtualMic.inf> | uninstall\n");
    return 2;
}
