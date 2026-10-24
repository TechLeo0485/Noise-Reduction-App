#pragma once

#include "audio/buffers/SampleConversion.h"
#include "audio/wasapi/WasapiCommon.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace vs {

enum class DeviceFlow { Capture, Render };

struct AudioDeviceInfo {
    std::wstring id;   // stable endpoint ID (persisted in settings)
    std::wstring name; // friendly name, e.g. "Microphone (USB Audio Device)"
    DeviceFlow flow = DeviceFlow::Capture;
    bool isDefault = false;               // default console device
    bool isDefaultCommunications = false; // default communications device
    bool isVirtualCable = false;          // heuristically detected virtual audio cable
    StreamFormat mixFormat{};             // shared-mode engine format
    bool formatSupported = false;
};

enum class DeviceEventType { Added, Removed, StateChanged, DefaultChanged };

struct DeviceEvent {
    DeviceEventType type;
    DeviceFlow flow; // meaningful for DefaultChanged
    std::wstring deviceId;
};

[[nodiscard]] std::wstring deviceFriendlyName(IMMDevice* device);

// Windows Core Audio endpoint enumeration plus hot-plug / default-device notifications.
//
// Notifications arrive on a Windows-owned thread; the callback must not block and must not
// call back into this object synchronously (it should post to another thread).
class DeviceManager {
public:
    using EventCallback = std::function<void(const DeviceEvent&)>;

    DeviceManager();
    ~DeviceManager();
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return enumerator_ != nullptr; }
    [[nodiscard]] std::vector<AudioDeviceInfo> enumerate(DeviceFlow flow) const;
    [[nodiscard]] std::wstring defaultDeviceId(DeviceFlow flow) const;

    // Replaces the event callback. Thread-safe.
    void setEventCallback(EventCallback callback);

private:
    class NotificationClient;
    void dispatch(const DeviceEvent& event);

    ComPtr<IMMDeviceEnumerator> enumerator_;
    NotificationClient* notificationClient_ = nullptr; // COM reference-counted
    std::mutex callbackMutex_;
    EventCallback callback_;
};

[[nodiscard]] bool looksLikeVirtualCable(const std::wstring& name);

} // namespace vs
