#include "hardware/HardwareInfo.h"

#include "audio/wasapi/WasapiCommon.h"

#include <dxgi.h>
#include <immintrin.h>
#include <intrin.h>

#include <array>
#include <cstring>
#include <vector>

namespace vs {

namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(' ');
    const auto last = s.find_last_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, last - first + 1);
}

unsigned countPhysicalCores() {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0) {
        return 0;
    }
    std::vector<std::uint8_t> buffer(length);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) {
        return 0;
    }
    unsigned cores = 0;
    for (DWORD offset = 0; offset < length;) {
        auto* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) {
            ++cores;
        }
        offset += entry->Size;
    }
    return cores;
}

} // namespace

std::string pciVendorName(std::uint32_t vendorId) {
    switch (vendorId) {
    case 0x10DE: return "NVIDIA";
    case 0x1002:
    case 0x1022: return "AMD";
    case 0x8086: return "Intel";
    case 0x5143: return "Qualcomm";
    case 0x1414: return "Microsoft";
    default: return "Unknown";
    }
}

CpuInfo detectCpu() {
    CpuInfo cpu;
    std::array<int, 4> regs{};

    __cpuid(regs.data(), 0);
    const int maxLeaf = regs[0];
    char vendor[13] = {};
    std::memcpy(vendor + 0, &regs[1], 4); // EBX
    std::memcpy(vendor + 4, &regs[3], 4); // EDX
    std::memcpy(vendor + 8, &regs[2], 4); // ECX
    cpu.vendor = vendor;

    __cpuid(regs.data(), static_cast<int>(0x80000000));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
        char brand[49] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs.data(), static_cast<int>(0x80000002 + i));
            std::memcpy(brand + i * 16, regs.data(), 16);
        }
        cpu.brand = trim(brand);
    }

    if (maxLeaf >= 1) {
        __cpuid(regs.data(), 1);
        const auto ecx = static_cast<unsigned>(regs[2]);
        cpu.sse41 = (ecx & (1u << 19)) != 0;
        const bool osxsave = (ecx & (1u << 27)) != 0;
        const bool avxHw = (ecx & (1u << 28)) != 0;
        const bool fmaHw = (ecx & (1u << 12)) != 0;
        unsigned long long xcr0 = 0;
        if (osxsave) {
            xcr0 = _xgetbv(0);
        }
        const bool osAvx = (xcr0 & 0x6) == 0x6;       // XMM + YMM state enabled by the OS
        const bool osAvx512 = (xcr0 & 0xE6) == 0xE6;  // + opmask / ZMM state
        cpu.avx = avxHw && osAvx;
        cpu.fma = fmaHw && osAvx;
        if (maxLeaf >= 7) {
            __cpuidex(regs.data(), 7, 0);
            const auto ebx = static_cast<unsigned>(regs[1]);
            cpu.avx2 = osAvx && (ebx & (1u << 5)) != 0;
            cpu.avx512f = osAvx512 && (ebx & (1u << 16)) != 0;
        }
    }

    cpu.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    cpu.physicalCores = countPhysicalCores();

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        cpu.totalMemoryBytes = mem.ullTotalPhys;
        cpu.availableMemoryBytes = mem.ullAvailPhys;
    }
    return cpu;
}

std::vector<GpuInfo> detectGpus() {
    std::vector<GpuInfo> gpus;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return gpus;
    }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) {
            continue;
        }
        GpuInfo gpu;
        gpu.index = static_cast<unsigned>(gpus.size());
        gpu.name = toUtf8(desc.Description);
        gpu.vendorId = desc.VendorId;
        gpu.deviceId = desc.DeviceId;
        gpu.vendorName = pciVendorName(desc.VendorId);
        gpu.dedicatedVideoMemory = desc.DedicatedVideoMemory;
        gpu.sharedSystemMemory = desc.SharedSystemMemory;
        gpu.luid = (static_cast<std::int64_t>(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart;
        gpu.isSoftwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        gpus.push_back(std::move(gpu));
    }
    return gpus;
}

HardwareInfo detectHardware() {
    HardwareInfo hw;
    hw.cpu = detectCpu();
    hw.gpus = detectGpus();

    ProcessingBackend cpu;
    cpu.id = "cpu";
    cpu.kind = ProcessorKind::Cpu;
    cpu.displayName = "CPU - " + (hw.cpu.brand.empty() ? hw.cpu.vendor : hw.cpu.brand);
    cpu.available = true;
    cpu.detail = "RNNoise and DSP (built in)";
    hw.backends.push_back(cpu);

    for (const GpuInfo& gpu : hw.gpus) {
        if (gpu.isSoftwareAdapter) {
            continue;
        }
        ProcessingBackend b;
        b.id = "gpu:" + std::to_string(gpu.index);
        b.kind = ProcessorKind::Gpu;
        b.displayName = "GPU " + std::to_string(gpu.index) + " - " + gpu.name;
        b.available = false;
        b.detail = "Detected. No GPU inference engine in this build (ONNX Runtime + DirectML is planned for Phase 3).";
        hw.backends.push_back(b);
    }

    // NPUs are not discoverable through DXGI adapter enumeration alone, and claiming one from
    // CPU branding would be wrong. Discovery requires an ONNX Runtime execution provider
    // (QNN / OpenVINO / VitisAI) or DXCore, which this build does not include.
    ProcessingBackend npu;
    npu.id = "npu";
    npu.kind = ProcessorKind::Npu;
    npu.displayName = "NPU";
    npu.available = false;
    npu.detail = "Not probed: NPU discovery needs an ONNX Runtime execution provider, which this build does not include.";
    hw.backends.push_back(npu);
    return hw;
}

} // namespace vs
