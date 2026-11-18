#pragma once

#include <auralis/audio_format.h>
#include <auralis/track_ref.h>

#include <QString>

#include <memory>

namespace auralis {

/// 一個處理器實例（例：ReplayGain）。每次開始播放一首曲目時由引擎向工廠建立，
/// 由解碼執行緒擁有與呼叫，不在即時音訊執行緒呼叫。
/// 任一處理器 isActive() 為 true 時，引擎把樣本轉成 64 位元浮點交給 process()，
/// 處理完再以 TPDF 抖動轉回輸出位元深度；全部都不作用時整數樣本直通（位元完美）。
class IProcessor {
public:
    virtual ~IProcessor() = default;

    /// 開始一首曲目前呼叫一次（解碼執行緒）。可以配置記憶體、讀設定。
    /// DSD 位元流不經過處理器，不會呼叫。
    virtual void prepare(const AudioFormat& format) = 0;
    /// 引擎實際呼叫的版本：多傳這一首的身分（例：ReplayGain 依曲目查增益）。無縫接續時拿到的是
    /// 接續的那一首。不需要曲目身分的處理器不用覆寫，預設轉給 prepare()。
    /// 不是從佇列播的音源（測試、自我檢查）track 的 path 是空的。
    virtual void prepareTrack(const AudioFormat& format, const TrackRef& track) {
        (void)track;
        prepare(format);
    }
    /// 這個處理器現在會不會改變訊號（例：ReplayGain 關閉時為 false）。
    /// 設定變更以原子變數生效，所以引擎每個區塊都會問一次；也可能從 UI 執行緒查詢狀態時呼叫。
    virtual bool isActive() const noexcept = 0;
    /// 改變訊號時給使用者看的原因（例：「ReplayGain 已套用」），用在輸出狀態的位元完美說明。
    /// 和 isActive() 一樣可能從別的執行緒呼叫，實作要能同時被讀。
    virtual QString reason() const = 0;
    /// 原地處理 frames 個樣本框：交錯、64 位元浮點、滿刻度為 ±1.0。
    /// 解碼執行緒；不配置記憶體、不丟例外、不 I/O。
    virtual void process(double* samples, qint64 frames, int channels) noexcept = 0;
};

/// 建立處理器的工廠，由模組在 start() 時註冊。
class IProcessorFactory {
public:
    virtual ~IProcessorFactory() = default;
    /// 給日誌與除錯看的名稱。
    virtual QString name() const = 0;
    /// 建立一個新的處理器實例（解碼執行緒，每首曲目一次）。
    virtual std::unique_ptr<IProcessor> create() = 0;
};

/// 處理器鏈：模組註冊的處理器依註冊順序串接，在解碼執行緒、寫入環形緩衝之前執行。
/// 執行緒：add() 在 UI 執行緒（模組 start() 期間）呼叫；建立實例由核心在解碼執行緒做。
class ProcessorChain {
public:
    virtual ~ProcessorChain() = default;
    ProcessorChain(const ProcessorChain&) = delete;
    ProcessorChain& operator=(const ProcessorChain&) = delete;

    /// 加到鏈的最後面。新加的處理器從下一首曲目開始作用。
    virtual void add(std::shared_ptr<IProcessorFactory> factory) = 0;

protected:
    ProcessorChain() = default;
};

} // namespace auralis
