// Tests that use real Windows audio endpoints and hardware. Label: "hardware".
// They never play audio to speakers: the engine is started without an output device
// (capture + processing + meters only) unless VOICESTUDIO_TEST_OUTPUT names an endpoint.

#include "audio/EngineSupervisor.h"
#include "devices/DeviceManager.h"
#include "hardware/HardwareInfo.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace vs;
using namespace std::chrono_literals;

namespace {
class HardwareTest : public ::testing::Test {
protected:
    ComInitializer com{COINIT_MULTITHREADED};
};

bool waitForState(EngineSupervisor& s, std::initializer_list<EngineState> states, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        const EngineState current = s.status().state;
        for (EngineState wanted : states) {
            if (current == wanted) {
                return true;
            }
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}
} // namespace

TEST_F(HardwareTest, DetectsCpuAndGpus) {
    const HardwareInfo hw = detectHardware();
    EXPECT_FALSE(hw.cpu.vendor.empty());
    EXPECT_GT(hw.cpu.logicalProcessors, 0u);
    EXPECT_GT(hw.cpu.physicalCores, 0u);
    EXPECT_GT(hw.cpu.totalMemoryBytes, 0u);
    ASSERT_FALSE(hw.backends.empty());
    EXPECT_EQ(hw.backends.front().id, "cpu");
    EXPECT_TRUE(hw.backends.front().available);
    for (const ProcessingBackend& b : hw.backends) {
        if (b.kind != ProcessorKind::Cpu) {
            EXPECT_FALSE(b.available) << "No GPU/NPU inference engine exists in this build";
        }
    }
}

TEST_F(HardwareTest, EnumeratesEndpointsWithStableIds) {
    DeviceManager devices;
    ASSERT_TRUE(devices.isValid());
    const auto inputs = devices.enumerate(DeviceFlow::Capture);
    const auto outputs = devices.enumerate(DeviceFlow::Render);
    for (const auto& d : inputs) {
        EXPECT_FALSE(d.id.empty());
        EXPECT_FALSE(d.name.empty());
    }
    // Enumerating twice yields the same IDs (they are persisted in settings).
    const auto again = devices.enumerate(DeviceFlow::Capture);
    ASSERT_EQ(again.size(), inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        EXPECT_EQ(again[i].id, inputs[i].id);
    }
    (void)outputs;
}

TEST_F(HardwareTest, CapturesAndProcessesFromDefaultMicrophone) {
    DeviceManager devices;
    if (devices.enumerate(DeviceFlow::Capture).empty()) {
        GTEST_SKIP() << "No microphone available";
    }
    EngineParameters params;
    EngineSupervisor supervisor(params);
    EngineConfig config; // default mic, no output
    if (const char* out = std::getenv("VOICESTUDIO_TEST_OUTPUT")) {
        config.outputDeviceId = fromUtf8(out);
    }
    supervisor.applyConfig(config);
    supervisor.launch();
    supervisor.setEnabled(true);
    ASSERT_TRUE(waitForState(supervisor, {EngineState::Running, EngineState::Degraded}, 5s))
        << supervisor.status().lastError;
    std::this_thread::sleep_for(2s);
    const EngineMetrics& m = supervisor.engine().metrics();
    EXPECT_GT(m.framesProcessed.load(), 150u); // 2 s = 200 frames of 10 ms
    EXPECT_EQ(m.nonFiniteRepairs.load(), 0u);
    supervisor.shutdown();
}

TEST_F(HardwareTest, RepeatedStartStopCycles) {
    DeviceManager devices;
    if (devices.enumerate(DeviceFlow::Capture).empty()) {
        GTEST_SKIP() << "No microphone available";
    }
    EngineParameters params;
    EngineSupervisor supervisor(params);
    supervisor.applyConfig(EngineConfig{});
    supervisor.launch();
    for (int cycle = 0; cycle < 20; ++cycle) {
        supervisor.setEnabled(true);
        ASSERT_TRUE(waitForState(supervisor, {EngineState::Running, EngineState::Degraded}, 5s)) << "cycle " << cycle;
        std::this_thread::sleep_for(100ms);
        supervisor.setEnabled(false);
        ASSERT_TRUE(waitForState(supervisor, {EngineState::Stopped}, 5s)) << "cycle " << cycle;
    }
    supervisor.shutdown();
}

TEST_F(HardwareTest, MissingDeviceReportsErrorWithoutCrashing) {
    EngineParameters params;
    EngineSupervisor supervisor(params);
    EngineConfig config;
    config.inputFollowDefault = false;
    config.inputDeviceId = L"{0.0.1.00000000}.{00000000-0000-0000-0000-000000000000}";
    config.autoRecover = false;
    supervisor.applyConfig(config);
    supervisor.launch();
    supervisor.setEnabled(true);
    EXPECT_TRUE(waitForState(supervisor, {EngineState::Error}, 5s));
    EXPECT_FALSE(supervisor.status().lastError.empty());
    supervisor.shutdown();
}
