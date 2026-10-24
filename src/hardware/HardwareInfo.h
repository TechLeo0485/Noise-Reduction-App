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

struct GpuInfo {
    unsigned index = 0;
    std::string name;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string vendorName;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t sharedSystemMemory = 0;
    std::int64_t luid = 0;
    bool isSoftwareAdapter = false; // e.g. Microsoft Basic Render Driver
};

enum class ProcessorKind { Cpu, Gpu, Npu };

// A processing backend the user can pick. `available` is true only when this build can
// actually run inference on it; unavailable entries carry the reason and are shown disabled.
struct ProcessingBackend {
    std::string id;          // "cpu", "gpu:0", "npu"
    std::string displayName; // "CPU - Intel(R) Core(TM) i7 ...", "GPU 0 - NVIDIA ..."
    ProcessorKind kind = ProcessorKind::Cpu;
    bool available = false;
    std::string detail;      // engines supported, or why unavailable
};

struct HardwareInfo {
    CpuInfo cpu;
    std::vector<GpuInfo> gpus;
    std::vector<ProcessingBackend> backends;
};

[[nodiscard]] CpuInfo detectCpu();
[[nodiscard]] std::vector<GpuInfo> detectGpus();
[[nodiscard]] HardwareInfo detectHardware();
[[nodiscard]] std::string pciVendorName(std::uint32_t vendorId);

} // namespace vs
