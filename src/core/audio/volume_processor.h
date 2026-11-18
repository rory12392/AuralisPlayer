#pragma once

#include <auralis/audio_processor.h>

#include <atomic>

namespace auralis::core {

/// 音量百分比（0–100）對應的線性增益：三次曲線 (p/100)³，聽感上接近等距
/// （50% 約 −18 dB、25% 約 −36 dB、10% −60 dB）；100% 是 1.0、0% 是 0（靜音）。
double volumeGain(int percent) noexcept;

/// 給使用者看的原因「音量 n%」（輸出狀態的非位元完美說明）。
QString volumeReasonText(int percent);

/// 音量處理器：播放引擎擁有一個，接在處理器鏈的最後面（ReplayGain 之後）。
/// 100% 而且沒有在過渡時不作用（isActive() 為 false），引擎照樣整數直通、維持位元完美。
/// 音量改變時增益以線性斜坡在約 30 毫秒內過渡到新值，沒有跳變與拉鍊雜訊；
/// 從 100% 改成別的值、或改回 100%，都是從目前的增益連續過渡，回到 1.0 之後才停止作用。
/// 使用者設的值（setPercent）要等解碼執行緒 latch() 才成為斜坡的目標：引擎在重做緩衝的那一刻
/// 才採用新值，斜坡的起點就落在重做的接續處，不會落在緩衝深處。
/// 執行緒：setPercent() 在 UI 執行緒；prepare()、process()、latch()、snap() 在解碼執行緒；
/// 其他查詢任何執行緒都可以（值都是原子變數）。
class VolumeProcessor final : public IProcessor {
public:
    /// 增益過渡的長度（毫秒）。
    static constexpr int kRampMs = 30;

    /// 使用者設的音量；percent 會限制在 0–100。
    void setPercent(int percent) noexcept;
    int percent() const noexcept { return m_percent.load(std::memory_order_acquire); }

    void prepare(const AudioFormat& format) override;
    /// 採用的目標不是 100%，或增益還在往 100% 過渡。
    bool isActive() const noexcept override;
    QString reason() const override { return volumeReasonText(percent()); }
    void process(double* samples, qint64 frames, int channels) noexcept override;

    /// 採用使用者設的值當作斜坡的目標。
    void latch() noexcept;
    /// 採用的目標是 0% 而且已經過渡完：引擎直接寫數位靜音（不加抖動）。
    bool isMuted() const noexcept;
    /// 採用使用者設的值，而且增益直接跳過去（開始播放、暫停後繼續時用：前面沒有聲音，不用過渡）。
    void snap() noexcept;
    /// 最近一次 process() 裡，增益還在變的樣本框數（0 表示整個區塊都是固定增益）。
    /// 引擎用它知道緩衝裡從哪裡開始已經是穩定的增益。
    qint64 lastRampFrames() const noexcept { return m_lastRampFrames; }

private:
    std::atomic<int> m_percent{100};
    /// 解碼執行緒採用的目標增益。
    std::atomic<double> m_target{1.0};
    /// 寫到目前為止實際套用的增益。
    std::atomic<double> m_current{1.0};
    // 以下只在解碼執行緒
    /// 斜坡每個樣本框的增益變化量與它對應的目標（目標換了就重算）。
    double m_step = 0.0;
    double m_stepTarget = 1.0;
    bool m_ramping = false;
    qint64 m_rampFrames = 1;
    qint64 m_lastRampFrames = 0;
};

} // namespace auralis::core
