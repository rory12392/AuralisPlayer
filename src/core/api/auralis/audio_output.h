#pragma once

#include <auralis/audio_format.h>
#include <auralis/dsd.h>
#include <auralis/result.h>

#include <QList>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>

namespace auralis {

/// 輸出後端在即時音訊執行緒取資料的來源（核心的播放引擎實作）。
class IRenderSource {
public:
    virtual ~IRenderSource() = default;
    /// 把 frames 個樣本框寫進 dest（格式是 IOutputBackend::open() 收到的 requested），
    /// 不夠的部分補靜音，所以 dest 一定被填滿；回傳其中真正來自曲目的樣本框數。
    /// 即時路徑：不配置記憶體、不加鎖、不 I/O、不丟例外，後端在自己的音訊執行緒呼叫。
    virtual qint64 render(std::byte* dest, qint64 frames) noexcept = 0;
};

/// 輸出失敗的種類，讓上層決定怎麼處理（訊息文字仍由介面用 qsTr 組）。
enum class OutputErrorKind {
    /// 其他原因（例：系統設定不允許獨占）：停止並顯示原因。
    Other,
    /// 裝置不支援這個取樣率或位元深度：顯示錯誤並跳過這首（AC-02-2，不重取樣）。
    FormatNotSupported,
    /// 裝置被別的程式佔用：停止並顯示原因，之後可以再按播放（AC-02-4）。
    DeviceBusy,
    /// 裝置被拔除或停用：停止並顯示原因，裝置回來後可以再按播放（AC-02-4）。
    DeviceLost,
};

/// 帶種類的輸出錯誤。
struct OutputError {
    OutputErrorKind kind = OutputErrorKind::Other;
    Error error;
};

/// 一個開啟中的輸出裝置（WASAPI 獨占、ASIO、Capture…）。由核心的播放引擎控制：
/// open → start → （播放）→ stop → close，可以再 open 另一個格式。
/// 執行緒：open、start、stop、close 在非即時執行緒呼叫（解碼或 UI 執行緒，同一時間只有一個）；
/// 後端在自己的即時音訊執行緒（WASAPI 事件執行緒、ASIO 驅動的回呼）呼叫 IRenderSource::render，
/// 那條執行緒要登記 MMCSS「Pro Audio」，並遵守即時路徑規則。
class IOutputBackend {
public:
    virtual ~IOutputBackend() = default;

    /// 給日誌與輸出狀態看的名稱（例：WASAPI 獨占）。
    virtual QString name() const = 0;
    /// 以 requested（來源的原生格式，整數左對齊在容器裡）開啟裝置，不重取樣。
    /// render 交出的一律是 requested 的排列；裝置要的打包方式（24-in-24、24-in-32）由後端在
    /// render 之後自己轉換。回傳裝置實際開啟的格式；不支援時回傳 Error（原因給日誌與介面）。
    virtual Result<AudioFormat> open(const AudioFormat& requested) = 0;
    /// 開始從 source 取資料。source 在 stop() 回傳前都有效。
    virtual Result<bool> start(IRenderSource& source) = 0;
    /// 停止呼叫 render，回傳時保證音訊執行緒不會再碰 source。沒有 start 時什麼都不做。
    virtual void stop() noexcept = 0;
    /// 釋放裝置。沒有 open 時什麼都不做。
    virtual void close() noexcept = 0;

    /// 最近一次 open() 或 start() 回傳 Error 的種類：格式不支援時上層跳過這首（AC-02-2），
    /// 被佔用或裝置不見時停止播放並顯示原因（AC-02-4）。沒有分類的後端一律回傳 Other。
    virtual OutputErrorKind lastErrorKind() const noexcept { return OutputErrorKind::Other; }
    /// 播放中裝置失敗（被拔除、被別的程式佔用）的原因；這時後端已經不再呼叫 render。
    /// 沒有失敗時為空。非即時執行緒輪詢（播放引擎轉交給上層）；stop()／close() 後仍可讀，
    /// 下一次 open() 清掉。後端在音訊執行緒只設原子旗標，Error 在這裡才組出來。
    virtual std::optional<OutputError> playbackError() const { return std::nullopt; }
    /// 輸出路徑本身會改變訊號時的原因（例：WASAPI 共享經 Windows 混音器，「共享模式」），
    /// 列進輸出狀態的非位元完美原因（AC-02-3）；不改變訊號的路徑回傳空字串。非即時執行緒呼叫。
    virtual QString signalChange() const { return {}; }
};

/// 一個可選的輸出裝置。
struct OutputDevice {
    /// 後端自己的裝置識別（例：WASAPI 的端點 ID、ASIO 驅動的 CLSID）。
    QString id;
    /// 給使用者看的名稱。
    QString name;
};

/// 建立輸出後端的工廠：核心的 WASAPI 獨占與共享、模組的 ASIO。由 OutputRegistry 保存。
class IOutputBackendFactory {
public:
    virtual ~IOutputBackendFactory() = default;
    /// 輸出路徑的名稱（例：WASAPI 獨占、ASIO）。
    virtual QString name() const = 0;
    /// 列出目前可用的裝置（可能要問系統，不在 UI 執行緒呼叫）。
    virtual QList<OutputDevice> devices() = 0;
    /// 建立指定裝置的後端（還沒開啟）；裝置不存在時回傳 Error。
    virtual Result<std::unique_ptr<IOutputBackend>> create(const QString& deviceId) = 0;
    /// 輸出面板裝置列右側的能力說明（例：「位元完美」「經 Windows 混音，非位元完美」）；
    /// 空字串時不顯示。任何非即時執行緒。
    virtual QString note() const { return {}; }
};

/// 輸出路徑的註冊表：核心先放 WASAPI 獨占與共享，模組在 start() 加自己的（例：ASIO）。
/// 也收進階輸出模組的 DSD 包裝器，播放 DSD 曲目時由核心交給播放引擎。
/// 執行緒：在 UI 執行緒（模組 start() 期間）呼叫；核心在別的執行緒讀時自己同步。
class OutputRegistry {
public:
    virtual ~OutputRegistry() = default;
    OutputRegistry(const OutputRegistry&) = delete;
    OutputRegistry& operator=(const OutputRegistry&) = delete;

    /// 加入一個輸出路徑；和已有的 name() 相同時取代它（位置不變）。
    virtual void add(std::shared_ptr<IOutputBackendFactory> factory) = 0;
    /// 設定 DSD 包裝器（DoP、原生 DSD、轉 PCM 由包裝器自己依設定決定）；nullptr 表示不能播 DSD。
    /// 從下一首開始生效。
    virtual void setDsdSink(std::shared_ptr<DsdSink> sink) = 0;

protected:
    OutputRegistry() = default;
};

} // namespace auralis
