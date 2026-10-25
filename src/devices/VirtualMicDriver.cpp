#include "devices/VirtualMicDriver.h"

#include <windows.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <cwchar>

namespace vs {

namespace {

constexpr wchar_t kHardwareId[] = L"Root\\VoiceStudioVirtualMic";

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

bool registryDword(const wchar_t* key, const wchar_t* value) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) == ERROR_SUCCESS &&
           data != 0;
}

bool bootedInTestMode() {
    wchar_t options[1024] = {};
    DWORD size = sizeof(options);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions", RRF_RT_REG_SZ,
                     nullptr, options, &size) != ERROR_SUCCESS) {
        return false;
    }
    _wcsupr_s(options);
    return wcsstr(options, L"TESTSIGNING") != nullptr;
}

} // namespace

VirtualMicStatus queryVirtualMicDriver() {
    VirtualMicStatus status;
    status.testMode = bootedInTestMode();
    status.secureBoot = registryDword(L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled");

    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return status;
    }
    SP_DEVINFO_DATA data{sizeof(data)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        if (!hasOurHardwareId(set, data)) {
            continue;
        }
        ULONG st = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&st, &problem, data.DevInst, 0) != CR_SUCCESS) {
            status.state = VirtualMicState::NotStarted;
        } else if ((st & DN_HAS_PROBLEM) != 0) {
            status.problemCode = problem;
            status.state = problem == CM_PROB_UNSIGNED_DRIVER ? VirtualMicState::Blocked : VirtualMicState::Problem;
        } else {
            status.state = (st & DN_STARTED) != 0 ? VirtualMicState::Running : VirtualMicState::NotStarted;
        }
        break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return status;
}

const char* virtualMicStateName(VirtualMicState state) noexcept {
    switch (state) {
    case VirtualMicState::NotInstalled: return "notInstalled";
    case VirtualMicState::Running: return "running";
    case VirtualMicState::Blocked: return "blocked";
    case VirtualMicState::Problem: return "problem";
    case VirtualMicState::NotStarted: return "notStarted";
    }
    return "notInstalled";
}

} // namespace vs
