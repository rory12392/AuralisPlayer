#pragma once

#include "audio/realtime_thread.h"

#include <auralis/audio_output.h>

#include <QByteArray>
#include <QList>

#include <atomic>
#include <memory>

namespace auralis::core {

/// 送進裝置的資料有沒有不連續（--soak 用）：PCM 看相鄰樣本差有沒有超過門檻（滿刻度的比例），
/// DoP 看標記 0x05／0xFA 有沒有一框一框交替、各聲道一致。每次開裝置以第一框判斷是哪一種：
/// 24-in-32 而且各聲道的最高位元組都是同一個 DoP 標記、最低位元組是 0 時當作 DoP；
/// 原生 DSD（沒有 PCM 取樣率）不檢查。feed() 在 Capture 的音訊執行緒：不配置、不加鎖。
class ContinuityCheck {
public:
    static constexpr int kMaxChannels = 8;

    explicit ContinuityCheck(double threshold = 0.0) : m_threshold(threshold) {}

    /// 開裝置時（音訊執行緒沒在跑）：換成這次的格式，累計的次數保留。
    void begin(const AudioFormat& format) noexcept;
    void feed(const std::byte* data, qint64 frames) noexcept;

    /// PCM 相鄰樣本差超過門檻的次數（每個聲道各算）。
    qint64 pcmJumps() const noexcept { return m_pcmJumps; }
    /// DoP 標記沒交替或不是 DoP 封包的框數。
    qint64 dopBreaks() const noexcept { return m_dopBreaks; }
    /// 看過的 PCM 相鄰樣本差最大值（滿刻度的比例）。
    double maxPcmStep() const noexcept { return m_maxPcmStep; }
    double threshold() const noexcept { return m_threshold; }

private:
    enum class Mode { Off, Unknown, Pcm, Dop };

    double sampleAt(const std::byte* frame, int channel) const noexcept;
    quint8 markerAt(const std::byte* frame, int channel) const noexcept;

    double m_threshold = 0.0;
    AudioFormat m_format;
    Mode m_mode = Mode::Off;
    bool m_havePrevious = false;
    double m_previous[kMaxChannels] = {};
    quint8 m_previousMarker = 0;
    qint64 m_pcmJumps = 0;
    qint64 m_dopBreaks = 0;
    double m_maxPcmStep = 0.0;
};

/// 測試用的輸出後端：不開實體裝置，以模擬的裝置節奏在即時音訊執行緒（MMCSS）呼叫 render，
/// 把送進「裝置緩衝」的位元組原封不動記下來，讓自動測試做位元比對與欠載計數。
/// 記錄的空間在 open() 一次配好，音訊執行緒裡不配置。
class CaptureBackend final : public IOutputBackend {
public:
    struct Options {
        /// 相對於實際時間的速度（2.0 表示裝置以兩倍速消耗資料），讓測試跑短一點。
        double speed = 1.0;
        /// 每次向來源要的樣本框數（模擬裝置緩衝大小）；0 表示 10 毫秒。
        qint64 periodFrames = 0;
        /// 最多記錄幾秒；之後照常消耗資料但不再記錄。
        int maxCaptureSeconds = 30;
        /// 不連續檢查的 PCM 門檻（滿刻度的比例）；0 表示不檢查。
        double continuityThreshold = 0.0;
    };

    CaptureBackend() : CaptureBackend(Options{}) {}
    explicit CaptureBackend(Options options);
    ~CaptureBackend() override;

    QString name() const override;
    /// 接受任何有效格式，裝置格式就是 requested。
    Result<AudioFormat> open(const AudioFormat& requested) override;
    Result<bool> start(IRenderSource& source) override;
    void stop() noexcept override;
    void close() noexcept override;

    /// 一次 open() 期間記下的內容與格式。
    struct Session {
        AudioFormat format;
        QByteArray data;
    };

    /// 記下的裝置緩衝內容（含曲目結束後補的靜音）。在 stop() 之後讀。
    QByteArray captured() const;
    /// 每次 open() 各一段，依時間順序（換格式重開裝置的測試用）；最後一段就是 captured()。
    QList<Session> sessions() const;
    /// render 回報的曲目樣本框數總和（不含補的靜音）。
    qint64 sourceFrames() const noexcept { return m_sourceFrames.load(std::memory_order_relaxed); }
    /// 音訊執行緒是否登記了 MMCSS「Pro Audio」。
    bool mmcssRegistered() const noexcept { return m_thread.mmcssRegistered(); }
    /// 音訊執行緒的 Windows 執行緒 ID（測試檢查即時路徑不配置記憶體用）；沒有在跑時為 0。
    unsigned long audioThreadId() const noexcept { return m_thread.nativeId(); }
    /// 不連續檢查的結果（Options::continuityThreshold > 0 時才有檢查）。在 stop() 之後讀。
    const ContinuityCheck& continuity() const noexcept { return m_continuity; }

private:
    bool step() noexcept;

    Options m_options;
    AudioFormat m_format;
    qint64 m_framesPerSecond = 0;
    qint64 m_periodFrames = 0;
    int m_frameBytes = 0;
    std::unique_ptr<std::byte[]> m_storage;
    qint64 m_storageBytes = 0;
    qint64 m_storedBytes = 0;
    std::unique_ptr<std::byte[]> m_period;
    QList<Session> m_history;
    IRenderSource* m_source = nullptr;
    qint64 m_consumedFrames = 0;
    qint64 m_startTicks = 0;
    RealtimeThread m_thread;
    ContinuityCheck m_continuity;
    std::atomic<qint64> m_sourceFrames{0};
};

} // namespace auralis::core
