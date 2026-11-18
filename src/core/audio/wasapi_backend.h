#pragma once

#include "audio/realtime_thread.h"
#include "audio/wasapi_endpoints.h"

#include <auralis/audio_output.h>

#include <QStringList>

#include <atomic>
#include <memory>

namespace auralis::core {

enum class WasapiMode {
    /// 獨占、事件驅動：以來源的取樣率與位元深度直接開裝置（位元完美）。
    Exclusive,
    /// 共享、事件驅動：經 Windows 混音器（自動轉換格式，不是位元完美），獨占不能用時的備援選項。
    Shared,
};

/// WASAPI 輸出後端。音訊執行緒是自己的 RealtimeThread（MMCSS「Pro Audio」），等裝置事件後
/// 向 IRenderSource 要一個裝置緩衝的資料；格式要打包（24-in-24、浮點轉整數）時先 render 到
/// open() 配好的暫存區再轉。裝置在播放中失敗時，音訊執行緒只記下 HRESULT 並結束，
/// 由 playbackError() 交給上層（AC-02-4）。
/// 執行緒：IOutputBackend 的規則；open() 每次都重新向 AudioEndpoints 取裝置，
/// 所以裝置拔掉再接回後，下一次 open() 就能用。
class WasapiBackend final : public IOutputBackend {
public:
    WasapiBackend(WasapiMode mode, std::shared_ptr<AudioEndpoints> endpoints, QString deviceId);
    ~WasapiBackend() override;
    WasapiBackend(const WasapiBackend&) = delete;
    WasapiBackend& operator=(const WasapiBackend&) = delete;

    QString name() const override;
    Result<AudioFormat> open(const AudioFormat& requested) override;
    Result<bool> start(IRenderSource& source) override;
    void stop() noexcept override;
    void close() noexcept override;
    OutputErrorKind lastErrorKind() const noexcept override { return m_lastErrorKind; }
    std::optional<OutputError> playbackError() const override;
    QString signalChange() const override;

    /// 音訊執行緒是否登記了 MMCSS（測試用）。
    bool mmcssRegistered() const noexcept { return m_thread.mmcssRegistered(); }
    /// 音訊執行緒的 Windows 執行緒 ID；沒有在跑時為 0（測試用）。
    unsigned long audioThreadId() const noexcept { return m_thread.nativeId(); }

private:
    Result<AudioFormat> openExclusive(const AudioFormat& requested);
    Result<AudioFormat> openShared(const AudioFormat& requested);
    /// 事件驅動獨占的初始化；緩衝大小沒對齊時以對齊後的週期換一個新的 IAudioClient 再試。
    Result<bool> initializeExclusive(const WAVEFORMATEXTENSIBLE& wave);
    /// Initialize 之後：事件、緩衝大小、IAudioRenderClient、轉換用的暫存區。
    Result<bool> prepareStream(const AudioFormat& requested, const AudioFormat& device);
    Result<bool> startStream(IRenderSource& source);
    /// 即時路徑：等一次裝置事件並填資料；裝置失敗時回傳 false 結束音訊執行緒。
    bool step() noexcept;
    /// 即時路徑：向來源要 frames 個樣本框填進裝置緩衝。
    bool fill(UINT32 frames) noexcept;

    WasapiMode m_mode;
    std::shared_ptr<AudioEndpoints> m_endpoints;
    QString m_deviceId;

    Microsoft::WRL::ComPtr<IAudioClient> m_client;
    Microsoft::WRL::ComPtr<IAudioRenderClient> m_render;
    HANDLE m_event = nullptr;
    // 以下在 open() 設定，播放中不變（音訊執行緒只讀）
    AudioFormat m_requested;
    AudioFormat m_device;
    bool m_convert = false;
    UINT32 m_bufferFrames = 0;
    std::unique_ptr<std::byte[]> m_scratch;
    IRenderSource* m_source = nullptr;

    OutputErrorKind m_lastErrorKind = OutputErrorKind::Other;
    std::atomic<long> m_failure{0};
    RealtimeThread m_thread;
};

/// WASAPI 獨占或共享的輸出路徑。name() 是「WASAPI 獨占」或「WASAPI 共享」。
class WasapiBackendFactory final : public IOutputBackendFactory {
public:
    explicit WasapiBackendFactory(WasapiMode mode,
                                  std::shared_ptr<AudioEndpoints> endpoints = systemEndpoints());

    QString name() const override;
    QList<OutputDevice> devices() override;
    Result<std::unique_ptr<IOutputBackend>> create(const QString& deviceId) override;
    QString note() const override;

    /// 唯讀查詢：獨占模式下裝置支援的常見格式（IsFormatSupported，不初始化、不發聲）。
    /// 給 --self-check --probe-output 看實機的格式協商依據。
    Result<QStringList> probeExclusiveFormats(const QString& deviceId);

private:
    WasapiMode m_mode;
    std::shared_ptr<AudioEndpoints> m_endpoints;
};

} // namespace auralis::core
