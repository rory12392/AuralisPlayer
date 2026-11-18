#include "audio/wasapi_endpoints.h"

// PKEY_Device_FriendlyName 的定義要在這個編譯單元產生
#include <initguid.h>

#include <propkeydef.h>

#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

using Microsoft::WRL::ComPtr;

namespace auralis::core {

namespace {

class MmDeviceEndpoints final : public AudioEndpoints {
public:
    MmDeviceEndpoints() {
        // 失敗時 m_cookie 為 nullptr：之後的呼叫端執行緒仍可自己初始化 COM
        CoIncrementMTAUsage(&m_cookie);
    }
    ~MmDeviceEndpoints() override {
        if (m_cookie != nullptr) {
            CoDecrementMTAUsage(m_cookie);
        }
    }
    MmDeviceEndpoints(const MmDeviceEndpoints&) = delete;
    MmDeviceEndpoints& operator=(const MmDeviceEndpoints&) = delete;

    Result<QList<OutputDevice>> list() override {
        ComPtr<IMMDeviceEnumerator> enumerator;
        AURALIS_HR_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDeviceCollection> collection;
        AURALIS_HR_CHECK(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection));
        UINT count = 0;
        AURALIS_HR_CHECK(collection->GetCount(&count));
        QList<OutputDevice> devices;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            AURALIS_HR_CHECK(collection->Item(i, &device));
            LPWSTR id = nullptr;
            AURALIS_HR_CHECK(device->GetId(&id));
            OutputDevice entry;
            entry.id = QString::fromWCharArray(id);
            CoTaskMemFree(id);
            entry.name = friendlyName(device.Get());
            devices.append(entry);
        }
        return devices;
    }

    Result<ComPtr<IAudioClient>> activate(const QString& id) override {
        ComPtr<IMMDeviceEnumerator> enumerator;
        AURALIS_HR_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDevice> device;
        AURALIS_HR_CHECK(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(id.utf16()), &device));
        DWORD state = 0;
        AURALIS_HR_CHECK(device->GetState(&state));
        if (state != DEVICE_STATE_ACTIVE) {
            // 拔除或停用的端點還查得到，只是不是啟用狀態：當成裝置不見了
            return hresultError(AUDCLNT_E_DEVICE_INVALIDATED, "IMMDevice::GetState");
        }
        ComPtr<IAudioClient> client;
        AURALIS_HR_CHECK(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(client.GetAddressOf())));
        return client;
    }

private:
    static QString friendlyName(IMMDevice* device) {
        ComPtr<IPropertyStore> store;
        if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
            return {};
        }
        PROPVARIANT value;
        PropVariantInit(&value);
        QString name;
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) {
            name = QString::fromWCharArray(value.pwszVal);
        }
        PropVariantClear(&value);
        return name;
    }

    CO_MTA_USAGE_COOKIE m_cookie = nullptr;
};

} // namespace

std::shared_ptr<AudioEndpoints> systemEndpoints() {
    return std::make_shared<MmDeviceEndpoints>();
}

} // namespace auralis::core
