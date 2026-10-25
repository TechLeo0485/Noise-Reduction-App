#include "audio/isolation/VoiceIsolator.h"
#include "hardware/HardwareInfo.h"

#include <gtest/gtest.h>

using namespace vs;

namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024;
constexpr std::uint64_t kGiB = 1024 * kMiB;

AdapterFacts gpu(const char* name, std::uint32_t vendor, std::uint64_t dedicated, std::uint64_t shared = 8 * kGiB) {
    AdapterFacts f;
    f.description = name;
    f.vendorId = vendor;
    f.dedicatedMemory = dedicated;
    f.sharedMemory = shared;
    return f;
}

struct Result {
    bool kept;
    AcceleratorKind kind;
    std::string reason;
};

Result classify(const AdapterFacts& f) {
    Result r{};
    r.kept = classifyAdapter(f, r.kind, r.reason);
    return r;
}

} // namespace

TEST(Hardware, KeepsGraphicsCardsOfEveryVendor) {
    for (const AdapterFacts& f : {gpu("NVIDIA GeForce RTX 4070", 0x10DE, 12 * kGiB),
                                  gpu("NVIDIA GeForce GTX 1050 Ti", 0x10DE, 4 * kGiB),
                                  gpu("NVIDIA GeForce RTX 3050 Laptop GPU", 0x10DE, 4 * kGiB),
                                  gpu("AMD Radeon RX 7900 XTX", 0x1002, 24 * kGiB),
                                  gpu("Intel(R) Arc(TM) A770 Graphics", 0x8086, 16 * kGiB)}) {
        const Result r = classify(f);
        EXPECT_TRUE(r.kept) << f.description << ": " << r.reason;
        EXPECT_EQ(r.kind, AcceleratorKind::DiscreteGpu) << f.description;
    }
}

TEST(Hardware, RecognisesIntegratedGraphics) {
    for (const AdapterFacts& f : {gpu("Intel(R) UHD Graphics 620", 0x8086, 128 * kMiB),
                                  gpu("Intel(R) Iris(R) Xe Graphics", 0x8086, 128 * kMiB),
                                  gpu("AMD Radeon 780M Graphics", 0x1002, 512 * kMiB),
                                  gpu("Qualcomm(R) Adreno(TM) X1-85 GPU", 0x4D4F4351, 0)}) {
        const Result r = classify(f);
        EXPECT_TRUE(r.kept) << f.description << ": " << r.reason;
        EXPECT_EQ(r.kind, AcceleratorKind::IntegratedGpu) << f.description;
    }
    // DXCore's answer wins over the memory heuristic (an APU with a large carve-out).
    AdapterFacts apu = gpu("AMD Radeon(TM) 890M", 0x1002, 2 * kGiB);
    apu.integratedKnown = true;
    apu.integrated = true;
    EXPECT_EQ(classify(apu).kind, AcceleratorKind::IntegratedGpu);
    AdapterFacts laptop = gpu("NVIDIA GeForce RTX 4060 Laptop GPU", 0x10DE, 8 * kGiB);
    laptop.integratedKnown = true;
    laptop.integrated = false;
    EXPECT_EQ(classify(laptop).kind, AcceleratorKind::DiscreteGpu);
}

TEST(Hardware, DropsVirtualRemoteAndSoftwareAdapters) {
    AdapterFacts basicRender = gpu("Microsoft Basic Render Driver", 0x1414, 0);
    basicRender.software = true;
    for (const AdapterFacts& f : {basicRender, gpu("Parsec Virtual Display Adapter", 0, 0, 0),
                                  gpu("Parsec Virtual Display Adapter", 0x1234, 64 * kMiB),
                                  gpu("Microsoft Remote Display Adapter", 0x1414, 0),
                                  gpu("Microsoft Basic Display Adapter", 0x1414, 0),
                                  gpu("VMware SVGA 3D", 0x15AD, 0, 2 * kGiB),
                                  gpu("Unknown Display", 0x1A03, 0, 0),
                                  gpu("Citrix Indirect Display Adapter", 0x5853, 0)}) {
        const Result r = classify(f);
        EXPECT_FALSE(r.kept) << f.description;
        EXPECT_FALSE(r.reason.empty()) << f.description;
    }
}

TEST(Hardware, RecognisesNpus) {
    AdapterFacts npu;
    npu.description = "Intel(R) AI Boost";
    npu.vendorId = 0x8086;
    npu.graphics = false;
    npu.compute = false;
    npu.genericMl = true;
    const Result r = classify(npu);
    EXPECT_TRUE(r.kept);
    EXPECT_EQ(r.kind, AcceleratorKind::Npu);
}

TEST(Hardware, DetectionListsCpuFirstAndNoSoftwareAdapters) {
    const HardwareInfo hw = detectHardware(true);
    ASSERT_FALSE(hw.backends.empty());
    EXPECT_EQ(hw.backends.front().id, "cpu");
    for (const GpuInfo& g : hw.gpus) {
        EXPECT_EQ(g.id.rfind("gpu:", 0), 0u) << g.id;
        EXPECT_EQ(g.name.find("Basic Render"), std::string::npos);
        EXPECT_EQ(g.name.find("Parsec"), std::string::npos);
    }
}

namespace {
ProcessorTrial trial(const char* id, int k, double p50, double p95) {
    ProcessorTrial t;
    t.id = id;
    t.name = id;
    t.framesPerRun = k;
    t.p50Ms = p50;
    t.p95Ms = p95;
    const double budget = k * 8.0;
    t.realTime = p50 <= 0.75 * budget && p95 <= budget;
    return t;
}
} // namespace

TEST(ProcessorChoice, LowestLatencyThatKeepsUpWins) {
    // CPU needs 24 ms chunks, the GPU keeps up with 16 ms: the GPU.
    EXPECT_EQ(pickProcessor({trial("cpu", 3, 15, 20), trial("gpu", 2, 9, 12)}), 1);
    // Both keep up with 16 ms: the one with clearly more headroom.
    EXPECT_EQ(pickProcessor({trial("cpu", 2, 8.4, 11.1), trial("gpu", 2, 11.7, 13.2)}), 0);
    EXPECT_EQ(pickProcessor({trial("cpu", 2, 11, 15), trial("gpu", 2, 5, 7)}), 1);
    // A tie stays on the CPU (listed first).
    EXPECT_EQ(pickProcessor({trial("cpu", 2, 8, 10), trial("gpu", 2, 8, 10.1)}), 0);
}

TEST(ProcessorChoice, KeepingUpBeatsSpeedAndFailuresAreSkipped) {
    // The CPU is too slow even at 40 ms; the GPU keeps up at 40 ms.
    EXPECT_EQ(pickProcessor({trial("cpu", 5, 35, 45), trial("gpu", 5, 23, 24)}), 1);
    // Nobody keeps up: the most headroom.
    EXPECT_EQ(pickProcessor({trial("cpu", 5, 38, 50), trial("gpu", 5, 36, 44)}), 1);
    ProcessorTrial broken;
    broken.id = "gpu";
    broken.error = "DirectML device lost";
    EXPECT_EQ(pickProcessor({broken, trial("cpu", 2, 8, 10)}), 1);
    EXPECT_EQ(pickProcessor({broken}), -1);
    EXPECT_EQ(pickProcessor({}), -1);
}
