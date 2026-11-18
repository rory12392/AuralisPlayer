#pragma once

#include <auralis/audio_format.h>
#include <auralis/result.h>
#include <auralis/track_ref.h>

#include <QString>
#include <QStringList>

#include <cstddef>
#include <memory>

namespace auralis {

/// 一個開啟中的音源（一個檔案或一首子曲目）。由解碼執行緒擁有與呼叫，可以 I/O 與配置記憶體；
/// 不在即時音訊執行緒呼叫。整數來源交出原始位元深度與取樣率的樣本，不轉浮點、不重取樣。
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    /// 開啟後就固定；串流中途改變格式時 read() 回傳 Error。
    virtual AudioFormat format() const = 0;
    /// 總長度（樣本框數）；不知道時為 -1。
    virtual qint64 lengthFrames() const = 0;
    /// 解碼最多 maxFrames 個樣本框寫進 dest（format().bytesPerFrame() × maxFrames 位元組），
    /// 回傳實際寫入的樣本框數；0 表示已經到結尾。檔案損毀到無法繼續時回傳 Error。
    virtual Result<qint64> read(std::byte* dest, qint64 maxFrames) = 0;
    /// 移到第 frame 個樣本框（從 0 起），之後的 read() 從那裡開始、樣本與從頭解碼到那裡相同。
    virtual Result<qint64> seek(qint64 frame) = 0;
};

/// 依副檔名建立音源的工廠。核心有 FFmpeg 的工廠；模組可以註冊自己的（例：DSF、DFF）。
class ISourceFactory {
public:
    virtual ~ISourceFactory() = default;
    /// 給日誌與除錯看的名稱。
    virtual QString name() const = 0;
    /// 支援的副檔名，小寫、不含點，例：{"dsf", "dff"}。
    virtual QStringList extensions() const = 0;
    /// 開啟檔案；格式不支援或檔案損毀時回傳 Error（message 給日誌，介面文字由呼叫端另外組）。
    /// 在解碼執行緒呼叫。
    virtual Result<std::unique_ptr<IAudioSource>> open(const QString& path) = 0;
};

/// 音源註冊表：副檔名 → 工廠。同一個副檔名時，模組註冊的優先於核心的 FFmpeg 音源，
/// 後註冊的模組優先於先註冊的。
/// 執行緒：add() 在 UI 執行緒（模組 start() 期間）呼叫；supports()、open() 可以在任何執行緒呼叫。
class SourceRegistry {
public:
    virtual ~SourceRegistry() = default;
    SourceRegistry(const SourceRegistry&) = delete;
    SourceRegistry& operator=(const SourceRegistry&) = delete;

    virtual void add(std::shared_ptr<ISourceFactory> factory) = 0;
    /// 有沒有工廠接受這個檔案的副檔名（不開檔）。
    virtual bool supports(const QString& path) const = 0;
    /// 用優先順序最高、接受這個副檔名的工廠開啟；沒有工廠接受時回傳 Error。
    virtual Result<std::unique_ptr<IAudioSource>> open(const QString& path) const = 0;
    /// 依曲目身分開音源，規則和播放相同：整檔（subsong 0）是整個檔案；CUE 曲目（.cue 路徑加軌號）
    /// 重新讀 .cue、開它指向的音訊檔，回傳的音源已經切成這一軌的範圍（這一軌的 INDEX 01 到下一軌的
    /// INDEX 01，AC-07-2）：位置 0 是這一軌的起點、lengthFrames() 是這一軌的長度、seek() 從這一軌的
    /// 起點算、讀到這一軌結尾就回傳 0。.cue 讀不到、沒有這一軌、音訊檔開不了時回傳 Error。
    /// 執行緒：任何非 UI 執行緒（會讀檔、開音源）。
    virtual Result<std::unique_ptr<IAudioSource>> openTrack(const TrackRef& track) const = 0;

protected:
    SourceRegistry() = default;
};

} // namespace auralis
