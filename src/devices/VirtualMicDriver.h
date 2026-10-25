#pragma once

namespace vs {

// State of the VoiceStudio Virtual Microphone driver (driver/, device Root\VoiceStudioVirtualMic).
enum class VirtualMicState {
    NotInstalled, // no device
    Running,      // driver loaded: the endpoints exist
    Blocked,      // installed, but Windows does not load the test-signed driver (Test Mode off): problem 52
    Problem,      // installed, another problem (see problemCode)
    NotStarted,   // installed, not started yet (e.g. restart pending)
};

struct VirtualMicStatus {
    VirtualMicState state = VirtualMicState::NotInstalled;
    unsigned long problemCode = 0; // Device Manager problem code when state is Problem / Blocked
    bool testMode = false;         // Windows booted with test signing (Test Mode)
    bool secureBoot = false;       // Secure Boot is on (Test Mode cannot be turned on)
};

// Queries Plug and Play and the boot configuration. Non-real-time; takes a few milliseconds.
[[nodiscard]] VirtualMicStatus queryVirtualMicDriver();

[[nodiscard]] const char* virtualMicStateName(VirtualMicState state) noexcept;

} // namespace vs
