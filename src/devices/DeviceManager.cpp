// initguid must precede the property-key headers in exactly one translation unit so the
// PKEY_* constants are defined here.
#include <initguid.h>

#include "devices/DeviceManager.h"

#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cwctype>

namespace vs {

namespace {

std::wstring lowercase(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

EDataFlow toEDataFlow(DeviceFlow flow) {
    return flow == DeviceFlow::Capture ? eCapture : eRender;
}

std::wstring defaultId(IMMDeviceEnumerator* enumerator, EDataFlow flow, ERole role) {
    ComPtr<IMMDevice> device;
    std::wstring id;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, role, &device))) {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(device->GetId(&raw)) && raw != nullptr) {
            id = raw;
            CoTaskMemFree(raw);
        }
    }
    return id;
}

} // namespace

bool looksLikeVirtualCable(const std::wstring& name) {
    const std::wstring n = lowercase(name);
    return n.find(L"voicestudio") != std::wstring::npos || // VoiceStudio Virtual Microphone (driver/)
           n.find(L"cable") != std::wstring::npos || n.find(L"vb-audio") != std::wstring::npos ||
           n.find(L"voicemeeter") != std::wstring::npos || n.find(L"voicemod") != std::wstring::npos ||
           n.find(L"virtual") != std::wstring::npos;
}

std::wstring deviceFriendlyName(IMMDevice* device) {
    std::wstring name;
    if (device == nullptr) {
        return name;
    }
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
        return name;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

// ---- IMMNotificationClient ------------------------------------------------------------------

class DeviceManager::NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(DeviceManager* owner) : owner_(owner) {}

    void detach() noexcept { owner_.store(nullptr); }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (r == 0) {
            delete this;
        }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD /*state*/) override {
        post({DeviceEventType::StateChanged, DeviceFlow::Capture, id ? id : L""});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override {
        post({DeviceEventType::Added, DeviceFlow::Capture, id ? id : L""});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override {
        post({DeviceEventType::Removed, DeviceFlow::Capture, id ? id : L""});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override {
        if (role == eConsole) {
            post({DeviceEventType::DefaultChanged, flow == eCapture ? DeviceFlow::Capture : DeviceFlow::Render,
                  id ? id : L""});
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void post(const DeviceEvent& e) {
        if (DeviceManager* owner = owner_.load()) {
            owner->dispatch(e);
        }
    }

    std::atomic<ULONG> refs_{1};
    std::atomic<DeviceManager*> owner_;
};

// ---- DeviceManager --------------------------------------------------------------------------

DeviceManager::DeviceManager() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) {
        enumerator_.Reset();
        return;
    }
    notificationClient_ = new NotificationClient(this);
    if (FAILED(enumerator_->RegisterEndpointNotificationCallback(notificationClient_))) {
        notificationClient_->Release();
        notificationClient_ = nullptr;
    }
}

DeviceManager::~DeviceManager() {
    if (notificationClient_ != nullptr) {
        notificationClient_->detach();
        if (enumerator_) {
            enumerator_->UnregisterEndpointNotificationCallback(notificationClient_);
        }
        notificationClient_->Release();
        notificationClient_ = nullptr;
    }
}

void DeviceManager::setEventCallback(EventCallback callback) {
    std::lock_guard lock(callbackMutex_);
    callback_ = std::move(callback);
}

void DeviceManager::dispatch(const DeviceEvent& event) {
    std::lock_guard lock(callbackMutex_);
    if (callback_) {
        callback_(event);
    }
}

std::wstring DeviceManager::defaultDeviceId(DeviceFlow flow) const {
    return enumerator_ ? defaultId(enumerator_.Get(), toEDataFlow(flow), eConsole) : std::wstring();
}

std::vector<AudioDeviceInfo> DeviceManager::enumerate(DeviceFlow flow) const {
    std::vector<AudioDeviceInfo> devices;
    if (!enumerator_) {
        return devices;
    }
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(toEDataFlow(flow), DEVICE_STATE_ACTIVE, &collection))) {
        return devices;
    }
    const std::wstring defaultConsole = defaultId(enumerator_.Get(), toEDataFlow(flow), eConsole);
    const std::wstring defaultComms = defaultId(enumerator_.Get(), toEDataFlow(flow), eCommunications);

    UINT count = 0;
    collection->GetCount(&count);
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) {
            continue;
        }
        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId)) || rawId == nullptr) {
            continue;
        }
        AudioDeviceInfo info;
        info.id = rawId;
        CoTaskMemFree(rawId);
        info.flow = flow;
        info.name = deviceFriendlyName(device.Get());
        if (info.name.empty()) {
            info.name = L"Unnamed audio device";
        }
        info.isDefault = info.id == defaultConsole;
        info.isDefaultCommunications = info.id == defaultComms;
        info.isVirtualCable = looksLikeVirtualCable(info.name);

        // Query the shared-mode format without starting a stream.
        ComPtr<IAudioClient> client;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
            WAVEFORMATEX* raw = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&raw))) {
                UniqueWaveFormat fmt(raw);
                info.mixFormat = toStreamFormat(fmt.get());
                info.formatSupported = info.mixFormat.isSupported();
            }
        }
        devices.push_back(std::move(info));
    }
    std::sort(devices.begin(), devices.end(),
              [](const AudioDeviceInfo& a, const AudioDeviceInfo& b) { return lowercase(a.name) < lowercase(b.name); });
    return devices;
}

} // namespace vs
