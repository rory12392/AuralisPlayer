#pragma once

// 假的 WASAPI 裝置：實作 IAudioClient／IAudioRenderClient，不開實體裝置也能測 WASAPI 後端的
// 格式協商、事件驅動、拔除與被佔用。COM 參考計數不管（生命週期由 FakeEndpoints 持有）。

#include "audio/wasapi_endpoints.h"

#include <QByteArray>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace auralis::test {

/// 一個假裝置的設定與紀錄；同一個裝置每次 activate 都產生新的 FakeAudioClient。
struct FakeDevice {
    QString id = QStringLiteral("fake-1");
    QString name = QStringLiteral("假的 DAC");
    /// 獨占模式支援哪些格式；預設全部支援。
    std::function<bool(const WAVEFORMATEXTENSIBLE&)> supports;
    /// false 表示被拔除：activate 與所有呼叫回傳 AUDCLNT_E_DEVICE_INVALIDATED。
    std::atomic<bool> present{true};
    /// 非 S_OK 時 Initialize 直接回傳它（例：AUDCLNT_E_DEVICE_IN_USE）。
    HRESULT initResult = S_OK;
    /// 非 0 時第一次獨占 Initialize 回傳 BUFFER_SIZE_NOT_ALIGNED，GetBufferSize 給這個框數。
    UINT32 alignedFrames = 0;
    bool alignmentReported = false;
    /// 送出這麼多個緩衝後拔除裝置（-1 表示不會）。
    int unplugAfterBuffers = -1;
    /// 每幾毫秒發一次裝置事件。
    int eventIntervalMs = 2;

    // 紀錄（測試在 stop 之後讀）
    std::mutex mutex;
    std::vector<WAVEFORMATEXTENSIBLE> initialized;
    std::vector<AUDCLNT_SHAREMODE> modes;
    std::vector<DWORD> flags;
    std::vector<REFERENCE_TIME> periods;
    int activations = 0;
    /// ReleaseBuffer 送出的位元組（預先配好，音訊執行緒不配置）。
    std::vector<std::byte> written = std::vector<std::byte>(64 * 1024 * 1024);
    std::atomic<std::size_t> writtenBytes{0};
    std::atomic<int> buffers{0};

    QByteArray writtenData() const {
        return QByteArray(reinterpret_cast<const char*>(written.data()),
                          static_cast<qsizetype>(writtenBytes.load()));
    }
};

class FakeAudioClient final : public IAudioClient, public IAudioRenderClient {
public:
    explicit FakeAudioClient(std::shared_ptr<FakeDevice> device) : m_device(std::move(device)) {}
    ~FakeAudioClient() { Stop(); }

    // IUnknown：生命週期由 FakeEndpoints 管
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // IAudioClient
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE mode, DWORD flags,
                                         REFERENCE_TIME duration, REFERENCE_TIME period,
                                         const WAVEFORMATEX* format, LPCGUID) override;
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* frames) override;
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME* latency) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* padding) override;
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE mode, const WAVEFORMATEX* format,
                                                WAVEFORMATEX** closest) override;
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** format) override;
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME* defaultPeriod,
                                              REFERENCE_TIME* minimumPeriod) override;
    HRESULT STDMETHODCALLTYPE Start() override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE event) override;
    HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void** out) override;

    // IAudioRenderClient
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 frames, BYTE** data) override;
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 frames, DWORD flags) override;

private:
    bool lost() const;

    std::shared_ptr<FakeDevice> m_device;
    bool m_initialized = false;
    UINT32 m_bufferFrames = 0;
    int m_blockAlign = 0;
    std::vector<std::byte> m_buffer;
    HANDLE m_event = nullptr;
    std::thread m_pump;
    std::atomic<bool> m_running{false};
};

/// 測試用的 AudioEndpoints：裝置清單與 activate 都來自 FakeDevice。
class FakeEndpoints final : public core::AudioEndpoints {
public:
    explicit FakeEndpoints(std::shared_ptr<FakeDevice> device) : m_device(std::move(device)) {}

    Result<QList<OutputDevice>> list() override;
    Result<Microsoft::WRL::ComPtr<IAudioClient>> activate(const QString& id) override;

private:
    std::shared_ptr<FakeDevice> m_device;
    std::vector<std::unique_ptr<FakeAudioClient>> m_clients;
};

/// 只支援這些 (取樣率, 容器位元, 有效位元) 的整數格式。
std::function<bool(const WAVEFORMATEXTENSIBLE&)>
supportsOnly(std::vector<std::tuple<DWORD, WORD, WORD>> formats);

} // namespace auralis::test
