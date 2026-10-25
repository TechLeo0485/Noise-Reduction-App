#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vs {

struct CpuInfo {
    std::string vendor;  // e.g. "GenuineIntel", "AuthenticAMD"
    std::string brand;   // full model string from CPUID
    unsigned physicalCores = 0;
    unsigned logicalProcessors = 0;
    bool sse41 = false;
    bool avx = false;
    bool avx2 = false;
    bool fma = false;
    bool avx512f = false;
    std::uint64_t totalMemoryBytes = 0;
    std::uint64_t availableMemoryBytes = 0;
};

// What kind of accelerator an adapter is.
enum class AcceleratorKind { DiscreteGpu, IntegratedGpu, Npu };

struct GpuInfo {
    unsigned index = 0;      // position in HardwareInfo::gpus (fastest first)
    std::string id;          // stable across restarts: "gpu:10de-2504-0" (vendor-device-ordinal); used in settings
    std::string name;        // e.g. "NVIDIA GeForce RTX 3060"
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string vendorName;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t sharedSystemMemory = 0;
    std::int64_t luid = 0;   // changes on every restart: never persist it
    AcceleratorKind kind = AcceleratorKind::DiscreteGpu;
    int dxgiIndex = -1;      // IDXGIFactory::EnumAdapters index = DirectML device id; -1 if not a DXGI adapter
    bool direct3d12 = false; // D3D12 feature level 11_0 or higher: what DirectML needs
};

// The facts about one adapter that decide whether it is a usable accelerator (see classifyAdapter()).
struct AdapterFacts {
    std::string description;
    std::uint32_t vendorId = 0;
    std::uint64_t dedicatedMemory = 0;
    std::uint64_t sharedMemory = 0;
    bool software = false;         // WARP / Basic Render (DXGI_ADAPTER_FLAG_SOFTWARE, DXCore IsHardware = false)
    bool integratedKnown = false;  // DXCore reported IsIntegrated
    bool integrated = false;
    bool graphics = true;          // D3D12 graphics (a GPU)
    bool compute = true;           // D3D12 compute
    bool genericMl = false;        // D3D12 generic ML only (an NPU)
};

// Keeps real GPUs and NPUs; drops software renderers and virtual / remote / indirect display adapters (Parsec,
// Remote Desktop, Basic Display, IddCx, ...), which cannot run inference. Integrated vs. discrete comes from DXCore
// when available, otherwise from the vendor and memory (Intel Arc and AMD Radeon cards have their own VRAM;
// Intel UHD/Iris and AMD APUs share system memory). Returns false with `reason` set for adapters it drops.
bool classifyAdapter(const AdapterFacts& facts, AcceleratorKind& kind, std::string& reason);

enum class ProcessorKind { Cpu, Gpu, Npu };

// A processing backend the user can pick. `available` is true only when this build can
// actually run inference on it; unavailable entries carry the reason and are shown disabled.
struct ProcessingBackend {
    std::string id;          // "cpu", or a GpuInfo::id
    std::string displayName; // "CPU - Intel(R) Core(TM) i7 ...", "NVIDIA GeForce RTX 3060 (discrete GPU, 12 GB)"
    ProcessorKind kind = ProcessorKind::Cpu;
    bool available = false;
    std::string detail;      // engines supported, or why unavailable
};

struct HardwareInfo {
    CpuInfo cpu;
    std::vector<GpuInfo> gpus;               // usable accelerators, fastest first (DXCore high-performance order)
    std::vector<std::string> ignoredAdapters; // "Parsec Virtual Display Adapter: virtual display adapter"
    std::vector<ProcessingBackend> backends;
};

[[nodiscard]] CpuInfo detectCpu();
// All adapters, classified. `ignored` (optional) receives the dropped ones with the reason.
[[nodiscard]] std::vector<GpuInfo> detectGpus(std::vector<std::string>* ignored = nullptr);
// `directMl`: this build ships the DirectML runtime (DirectML.dll next to the exe), so GPUs can run inference.
[[nodiscard]] HardwareInfo detectHardware(bool directMl = false);
[[nodiscard]] std::string pciVendorName(std::uint32_t vendorId);

} // namespace vs
