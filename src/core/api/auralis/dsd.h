#pragma once

#include <auralis/audio_format.h>
#include <auralis/result.h>

#include <QString>

#include <cstddef>

namespace auralis {

/// DSD 音源交出的位元流排列（核心與模組的約定）：
/// AudioFormat 的 dsdRate 是 DSD 取樣率（DSD64 為 2822400）、rate 為 0、
/// container 為 32、bits 為 1；
/// 一個「樣本框」是每個聲道 32 個 DSD 位元（4 個位元組），聲道以 4 位元組為單位交錯；
/// 位元依時間先後從第一個位元組的最高位元開始（MSB first）。DSF 是 LSB first、以區塊分聲道，
/// 由 DSF 音源在讀檔時轉成這個排列。
struct DsdFormat {
    /// DSD 取樣率（Hz）：DSD64 2822400、DSD128 5644800、DSD256 11289600。
    quint32 dsdRate = 0;
    quint16 channels = 0;

    /// 相對於 CD 取樣率的倍數（DSD64 為 64）。
    quint32 multiplier() const noexcept { return dsdRate / 44100; }
    /// 每個樣本框的位元組數（每聲道 4 個位元組）。
    int bytesPerFrame() const noexcept { return channels * 4; }
    bool isValid() const noexcept { return dsdRate > 0 && channels > 0; }

    /// 從 DSD 音源的 AudioFormat 取出；不是 DSD 時回傳的 isValid() 為 false。
    static DsdFormat from(const AudioFormat& format) noexcept {
        DsdFormat dsd;
        if (format.dsdRate > 0 && format.container == 32) {
            dsd.dsdRate = format.dsdRate;
            dsd.channels = format.channels;
        }
        return dsd;
    }

    friend bool operator==(const DsdFormat&, const DsdFormat&) = default;
};

/// DSD 位元流的包裝器：把 DSD 音源的位元流轉成輸出後端吃的格式（DoP、原生 DSD、轉 PCM）。
/// 實作在進階輸出模組；核心的播放引擎在解碼執行緒呼叫，DSD 不經過處理器鏈；
/// 音量只作用在轉 PCM 的輸出上（convertsToPcm()）。
class DsdSink {
public:
    virtual ~DsdSink() = default;
    DsdSink() = default;
    DsdSink(const DsdSink&) = delete;
    DsdSink& operator=(const DsdSink&) = delete;

    /// 給日誌與輸出狀態看的名稱（例：DoP）。
    virtual QString name() const = 0;
    /// 開始一首 DSD 曲目（解碼執行緒，可以配置記憶體）。回傳要交給輸出後端的格式：
    /// DoP 是 PCM（rate = dsdRate ÷ 16、bits 24、container 32），原生 DSD 是 DSD 格式本身，
    /// 轉 PCM 是 PCM。不支援這個 DSD 格式時回傳 Error。
    virtual Result<AudioFormat> open(const DsdFormat& format) = 0;
    /// dsdFrames 個 DSD 樣本框最多會轉出幾個輸出樣本框（引擎據此預先配置緩衝；DoP 是 2 倍）。
    virtual qint64 maxOutputFrames(qint64 dsdFrames) const noexcept = 0;
    /// 把 dsdFrames 個 DSD 樣本框轉成 open() 回傳的格式寫進 out，回傳寫入的輸出樣本框數。
    /// 解碼執行緒；不配置記憶體、不丟例外。
    virtual qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept = 0;
    /// 改變訊號時的原因（例：「DSD 轉 PCM」），列進輸出狀態的非位元完美原因（AC-02-3）；
    /// DoP 與原生 DSD 不改變位元流，回傳空字串。非即時執行緒呼叫。
    virtual QString reason() const { return {}; }
    /// 輸出是不是轉換出來的 PCM 訊號（轉 PCM）：是的話播放引擎在包裝器之後套用音量。
    /// DoP 雖然包成 PCM 的格式，內容仍是 DSD 位元流，任何增益都會把它變成噪音，所以是 false。
    /// 預設依 reason() 是否為空：會改變訊號的包裝器就是轉 PCM（DoP、原生 DSD 回傳空字串）。
    /// 非即時執行緒呼叫。
    virtual bool convertsToPcm() const { return !reason().isEmpty(); }
};

} // namespace auralis
