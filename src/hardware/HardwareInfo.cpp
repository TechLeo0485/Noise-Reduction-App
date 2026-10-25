#include "hardware/HardwareInfo.h"

#include "audio/wasapi/WasapiCommon.h"

#include <d3d12.h>
#include <dxgi.h>
// The DXCore attribute GUIDs are defined here (selectany).
#include <initguid.h>
#include <dxcore.h>
#include <immintrin.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
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
    case 0x5143:
    case 0x4D4F4351: return "Qualcomm";
    case 0x13B5: return "ARM";
    case 0x1ED5: return "Moore Threads";
    case 0x1414: return "Microsoft";
    case 0x15AD: return "VMware";
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

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Display adapters that exist only to show a desktop somewhere else: no inference on them.
constexpr std::array<const char*, 14> kVirtualAdapterNames = {
    "virtual display", "remote display", "parsec", "basic render", "basic display", "indirect display", "iddcx",
    "spacedesk", "displaylink", "citrix", "hyper-v", "virtualbox", "vmware svga", "remote desktop"};

bool knownVirtualVendor(std::uint32_t vendorId) {
    switch (vendorId) {
    case 0x1414: // Microsoft (Basic Render / Basic Display / Remote Display)
    case 0x15AD: // VMware
    case 0x80EE: // VirtualBox
    case 0x1AB8: // Parallels
    case 0x1AF4: // Red Hat virtio
    case 0x1B36: // QEMU
        return true;
    default:
        return false;
    }
}

// D3D12 at feature level 11_0 is what DirectML needs (NVIDIA Kepler/GTX 600+, AMD GCN+, Intel Haswell+).
bool supportsDirect3D12(IUnknown* adapter) {
    using CreateFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    static const auto create = [] {
        HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return d3d12 != nullptr ? reinterpret_cast<CreateFn>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    }();
    // With a null device pointer D3D12CreateDevice only tests support (S_FALSE), without creating a device.
    return create != nullptr && SUCCEEDED(create(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
}

struct RawAdapter {
    AdapterFacts facts;
    std::uint32_t deviceId = 0;
    std::int64_t luid = 0;
    bool direct3d12 = false;
};

std::int64_t luidValue(const LUID& luid) {
    return (static_cast<std::int64_t>(luid.HighPart) << 32) | luid.LowPart;
}

// DXCore (Windows 10 2004+): hardware/integrated flags, NPUs, and the system's high-performance order.
bool enumerateDxCore(std::vector<RawAdapter>& out) {
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE dxcore = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto createFactory =
        dxcore != nullptr ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxcore, "DXCoreCreateAdapterFactory")) : nullptr;
    ComPtr<IDXCoreAdapterFactory> factory;
    if (createFactory == nullptr || FAILED(createFactory(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    std::vector<std::int64_t> seen;
    for (const GUID& attribute : {DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML}) {
        ComPtr<IDXCoreAdapterList> list;
        if (FAILED(factory->CreateAdapterList(1, &attribute, IID_PPV_ARGS(&list)))) {
            continue;
        }
        const DXCoreAdapterPreference order[] = {DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::HighPerformance};
        list->Sort(2, order);
        for (uint32_t i = 0; i < list->GetAdapterCount(); ++i) {
            ComPtr<IDXCoreAdapter> adapter;
            if (FAILED(list->GetAdapter(i, IID_PPV_ARGS(&adapter)))) {
                continue;
            }
            RawAdapter raw;
            LUID luid{};
            adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &luid);
            raw.luid = luidValue(luid);
            if (std::find(seen.begin(), seen.end(), raw.luid) != seen.end()) {
                continue; // listed by both attributes
            }
            seen.push_back(raw.luid);
            size_t size = 0;
            if (SUCCEEDED(adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription, &size)) && size > 0) {
                std::string description(size, '\0');
                adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, size, description.data());
                raw.facts.description = trim(description.c_str());
            }
            DXCoreHardwareIDParts parts{};
            DXCoreHardwareID id{};
            if (adapter->IsPropertySupported(DXCoreAdapterProperty::HardwareIDParts) &&
                SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::HardwareIDParts, &parts))) {
                raw.facts.vendorId = parts.vendorID;
                raw.deviceId = parts.deviceID;
            } else if (SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::HardwareID, &id))) {
                raw.facts.vendorId = id.vendorID;
                raw.deviceId = id.deviceID;
            }
            uint64_t dedicated = 0;
            uint64_t shared = 0;
            adapter->GetProperty(DXCoreAdapterProperty::DedicatedAdapterMemory, &dedicated);
            adapter->GetProperty(DXCoreAdapterProperty::SharedSystemMemory, &shared);
            raw.facts.dedicatedMemory = dedicated;
            raw.facts.sharedMemory = shared;
            bool hardware = true;
            adapter->GetProperty(DXCoreAdapterProperty::IsHardware, &hardware);
            raw.facts.software = !hardware;
            bool integrated = false;
            if (SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::IsIntegrated, &integrated))) {
                raw.facts.integratedKnown = true;
                raw.facts.integrated = integrated;
            }
            raw.facts.graphics = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
            raw.facts.compute = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
            raw.facts.genericMl = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML);
            raw.direct3d12 = raw.facts.graphics && supportsDirect3D12(adapter.Get());
            out.push_back(std::move(raw));
        }
    }
    return true;
}

// DXGI fallback (no DXCore): GPUs only, no NPUs; integrated vs. discrete from vendor and memory.
void enumerateDxgi(IDXGIFactory1* factory, std::vector<RawAdapter>& out) {
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) {
            continue;
        }
        RawAdapter raw;
        raw.facts.description = trim(toUtf8(desc.Description));
        raw.facts.vendorId = desc.VendorId;
        raw.facts.dedicatedMemory = desc.DedicatedVideoMemory;
        raw.facts.sharedMemory = desc.SharedSystemMemory;
        raw.facts.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        raw.deviceId = desc.DeviceId;
        raw.luid = luidValue(desc.AdapterLuid);
        raw.direct3d12 = supportsDirect3D12(adapter.Get());
        out.push_back(std::move(raw));
    }
    // Without DXCore's order: discrete before integrated, then more video memory first.
    std::stable_sort(out.begin(), out.end(), [](const RawAdapter& a, const RawAdapter& b) {
        return a.facts.dedicatedMemory > b.facts.dedicatedMemory;
    });
}

} // namespace

bool classifyAdapter(const AdapterFacts& f, AcceleratorKind& kind, std::string& reason) {
    const std::string name = lower(f.description);
    if (f.software) {
        reason = "software renderer";
        return false;
    }
    for (const char* virtualName : kVirtualAdapterNames) {
        if (name.find(virtualName) != std::string::npos) {
            reason = "virtual or remote display adapter";
            return false;
        }
    }
    if (knownVirtualVendor(f.vendorId)) {
        reason = "virtual or basic display adapter";
        return false;
    }
    if (!f.graphics) {
        if (f.compute || f.genericMl) {
            kind = AcceleratorKind::Npu; // compute / ML only: Intel AI Boost, AMD XDNA, Qualcomm Hexagon
            return true;
        }
        reason = "no compute support";
        return false;
    }
    if (f.dedicatedMemory == 0 && f.sharedMemory == 0) {
        reason = "display-only adapter";
        return false;
    }
    bool integrated = f.integrated;
    if (!f.integratedKnown) {
        constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
        switch (f.vendorId) {
        case 0x8086: integrated = f.dedicatedMemory < kGiB; break;  // Intel UHD/Iris vs. Arc
        case 0x1002:
        case 0x1022: integrated = f.dedicatedMemory <= kGiB; break; // AMD APU carve-out vs. Radeon cards
        case 0x10DE: integrated = false; break;                     // NVIDIA: discrete (Tegra is not on Windows)
        default: integrated = f.dedicatedMemory < kGiB; break;      // Qualcomm Adreno, ARM Mali, ...
        }
    }
    kind = integrated ? AcceleratorKind::IntegratedGpu : AcceleratorKind::DiscreteGpu;
    return true;
}

std::vector<GpuInfo> detectGpus(std::vector<std::string>* ignored) {
    std::vector<GpuInfo> gpus;
    ComPtr<IDXGIFactory1> dxgi;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)))) {
        dxgi.Reset();
    }
    std::vector<RawAdapter> raw;
    if (!enumerateDxCore(raw) && dxgi) {
        enumerateDxgi(dxgi.Get(), raw);
    }

    // DirectML addresses GPUs by their DXGI enumeration index: match by LUID.
    std::vector<std::int64_t> dxgiLuids;
    if (dxgi) {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (dxgi->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_ADAPTER_DESC1 desc{};
            dxgiLuids.push_back(SUCCEEDED(adapter->GetDesc1(&desc)) ? luidValue(desc.AdapterLuid) : 0);
        }
    }

    for (const RawAdapter& r : raw) {
        AcceleratorKind kind{};
        std::string reason;
        if (!classifyAdapter(r.facts, kind, reason)) {
            if (ignored != nullptr) {
                ignored->push_back((r.facts.description.empty() ? std::string("Unnamed adapter") : r.facts.description) +
                                   ": " + reason);
            }
            continue;
        }
        GpuInfo gpu;
        gpu.index = static_cast<unsigned>(gpus.size());
        gpu.name = r.facts.description.empty() ? pciVendorName(r.facts.vendorId) + " adapter" : r.facts.description;
        gpu.vendorId = r.facts.vendorId;
        gpu.deviceId = r.deviceId;
        gpu.vendorName = pciVendorName(r.facts.vendorId);
        gpu.dedicatedVideoMemory = r.facts.dedicatedMemory;
        gpu.sharedSystemMemory = r.facts.sharedMemory;
        gpu.luid = r.luid;
        gpu.kind = kind;
        gpu.direct3d12 = r.direct3d12;
        const auto it = std::find(dxgiLuids.begin(), dxgiLuids.end(), r.luid);
        gpu.dxgiIndex = it != dxgiLuids.end() ? static_cast<int>(it - dxgiLuids.begin()) : -1;
        // Stable id: two identical cards get ordinals 0 and 1 in the (stable) high-performance order.
        int ordinal = 0;
        for (const GpuInfo& other : gpus) {
            ordinal += other.vendorId == gpu.vendorId && other.deviceId == gpu.deviceId ? 1 : 0;
        }
        char id[48];
        std::snprintf(id, sizeof(id), "gpu:%04x-%04x-%d", gpu.vendorId, gpu.deviceId, ordinal);
        gpu.id = id;
        gpus.push_back(std::move(gpu));
    }
    return gpus;
}

HardwareInfo detectHardware(bool directMl) {
    HardwareInfo hw;
    hw.cpu = detectCpu();
    hw.gpus = detectGpus(&hw.ignoredAdapters);

    ProcessingBackend cpu;
    cpu.id = "cpu";
    cpu.kind = ProcessorKind::Cpu;
    cpu.displayName = "CPU - " + (hw.cpu.brand.empty() ? hw.cpu.vendor : hw.cpu.brand);
    cpu.available = true;
    cpu.detail = "Voice isolation, noise removal and effects";
    hw.backends.push_back(cpu);

    for (const GpuInfo& gpu : hw.gpus) {
        ProcessingBackend b;
        b.id = gpu.id;
        const double gib = static_cast<double>(gpu.dedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
        char memory[32] = "";
        if (gpu.kind != AcceleratorKind::Npu && gib >= 0.25) {
            std::snprintf(memory, sizeof(memory), ", %.0f GB", gib < 1.0 ? 1.0 : gib);
        }
        const char* kindName = gpu.kind == AcceleratorKind::DiscreteGpu    ? "graphics card"
                               : gpu.kind == AcceleratorKind::IntegratedGpu ? "integrated graphics"
                                                                            : "NPU";
        b.displayName = gpu.name + " (" + kindName + memory + ")";
        if (gpu.kind == AcceleratorKind::Npu) {
            b.kind = ProcessorKind::Npu;
            b.available = false;
            b.detail = "Detected. NPUs are not supported for voice isolation yet.";
        } else if (!directMl) {
            b.kind = ProcessorKind::Gpu;
            b.available = false;
            b.detail = "Detected. This build has no GPU engine (DirectML).";
        } else if (!gpu.direct3d12 || gpu.dxgiIndex < 0) {
            b.kind = ProcessorKind::Gpu;
            b.available = false;
            b.detail = "Detected, but it does not support DirectX 12, which GPU voice isolation needs.";
        } else {
            b.kind = ProcessorKind::Gpu;
            b.available = true;
            b.detail = "Voice isolation (DirectML). Noise removal and effects stay on the CPU.";
        }
        hw.backends.push_back(b);
    }
    return hw;
}

} // namespace vs
