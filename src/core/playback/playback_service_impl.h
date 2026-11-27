#pragma once

#include "audio/next_track.h"
#include "audio/output_registry_impl.h"
#include "audio/playback_engine.h"
#include "audio/processor_chain_impl.h"
#include "audio/source_registry_impl.h"
#include "playback/playback_queue.h"

#include <auralis/playback_service.h>

#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace auralis::core {

/// 毫秒與樣本框換算用的每秒樣本框數（PlaybackService 的幾個實作檔共用）。
inline qint64 framesPerSecond(const AudioFormat& format) {
    return format.rate > 0 ? format.rate : format.dsdRate / 32;
}

/// PlaybackService 的實作：佇列（PlaybackQueue）＋播放引擎＋下一首提供者。
/// UI 執行緒擁有；引擎在解碼執行緒向它要下一首（nextTrackAfter），那裡只讀佇列、開檔、記 token。
/// 正在聽哪一首、播完沒、裝置錯誤都由 50 毫秒的計時器輪詢引擎得知（poll）。
/// 開始播放（開檔、探測格式、開裝置、預填緩衝）在自己的背景執行緒做，UI 執行緒只送出要求，
/// 完成時在 UI 執行緒收到結果再發狀態訊號（Architecture §5）。要求有序號：之後的操作（播放別首、
/// 停止、跳轉、暫停）取代還沒完成的要求，舊的結果晚到時丟掉。要求還沒完成時引擎歸背景執行緒，
/// UI 執行緒不碰它（音量除外）。
class PlaybackServiceImpl final : public PlaybackService, private NextTrackProvider {
    Q_OBJECT

public:
    PlaybackServiceImpl(SourceRegistryImpl& sources, OutputRegistryImpl& outputs,
                        ProcessorChainImpl& processors, QObject* parent = nullptr);
    /// seed 決定隨機播放的順序（測試用）。
    PlaybackServiceImpl(SourceRegistryImpl& sources, OutputRegistryImpl& outputs,
                        ProcessorChainImpl& processors, quint32 seed, QObject* parent = nullptr);
    ~PlaybackServiceImpl() override;

    void playFrom(const QList<TrackRef>& tracks, int index) override;
    void enqueueNext(const QList<TrackRef>& tracks) override;
    void enqueueLast(const QList<TrackRef>& tracks) override;
    void playAt(int index) override;
    void remove(const QList<quint64>& ids) override;
    void move(int from, int to) override;
    void clear() override;
    QList<QueueEntry> queue() const override { return m_queue.entries(); }
    int currentIndex() const override { return m_queue.currentIndex(); }
    std::optional<QueueEntry> currentEntry() const override { return m_queue.current(); }

    void play() override;
    void pause() override;
    void togglePause() override;
    void stop() override;
    void next() override;
    void previous() override;
    void seek(qint64 positionMs) override;

    State state() const override { return m_state; }
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }
    RepeatMode repeatMode() const override { return m_queue.repeat(); }
    void setRepeatMode(RepeatMode mode) override;
    bool shuffle() const override { return m_queue.shuffle(); }
    void setShuffle(bool on) override;
    OutputStatus outputStatus() const override { return m_output; }

    int volume() const override { return m_engine.volume(); }
    void setVolume(int percent) override;
    bool volumeAvailable() const override { return m_volumeAvailable; }
    QString volumeUnavailableReason() const override;

    /// 核心內部：指定輸出後端（測試用 Capture；之後輸出面板選裝置時用）。會先停止播放。
    /// 沒有指定時，第一次播放用 OutputRegistry 的第一個輸出路徑的第一個裝置。
    void setOutputBackend(std::unique_ptr<IOutputBackend> backend);

    /// 輸出面板的一個選項：輸出路徑（工廠名稱）＋裝置。
    struct OutputChoice {
        QString backend;
        QString deviceId;
        QString deviceName;
        friend bool operator==(const OutputChoice&, const OutputChoice&) = default;
    };
    /// 核心內部：換輸出路徑或裝置（輸出面板，AC-02-6），播放中時從目前位置繼續。
    /// 新裝置開不了（被佔用、不見了）時換回原本的輸出、從原位置繼續播，回傳原因；
    /// 新裝置不支援目前曲目的格式時照 AC-02-2 跳到下一首。停止或暫停時只記下選擇。
    Result<bool> switchOutput(const OutputChoice& choice);
    /// 目前選的輸出；還沒選、也還沒播過時是空的（第一次播放時選第一個輸出路徑的第一個裝置）。
    OutputChoice outputChoice() const { return m_choice; }
    /// 核心內部：輪詢一次引擎（計時器每 50 毫秒呼叫；測試直接呼叫）。
    void poll();
    /// 核心內部：等送出的開始播放要求在背景做完（含開不了而接著送出的下一首），結果在這裡處理，
    /// 回傳時狀態訊號都已發出。會等開檔，所以只給換輸出（switchOutput）、命令列截圖與測試用。
    void waitForStart();
    /// 核心內部：給測試讀欠載次數。
    const PlaybackEngine& engine() const { return m_engine; }

private:
    /// 交給引擎的每一首的資訊（token 每次交出都不同，重複單曲重播也是新的）。
    struct TokenInfo {
        quint64 entryId = 0;
        qint64 durationMs = -1;
        /// 音源每秒幾個樣本框（毫秒與樣本框換算）。
        qint64 sourceRate = 0;
    };

    std::optional<EngineTrack> nextTrack() override { return std::nullopt; }
    std::optional<EngineTrack> nextTrackAfter(quint64 token) override;

    /// 背景執行緒的一個要求：開始播放 entry，或（entry 空的）只把引擎停下來。
    struct StartJob {
        quint64 seq = 0;
        std::optional<QueueEntry> entry;
        qint64 offsetMs = 0;
        IOutputBackend* backend = nullptr;
        std::shared_ptr<DsdSink> dsdSink;
    };
    /// 要求做完的結果（交回 UI 執行緒）。
    struct StartResult {
        quint64 seq = 0;
        std::optional<QueueEntry> entry;
        qint64 offsetMs = 0;
        /// 開不了檔（AC-01-3）。
        std::optional<Error> openError;
        /// 開了檔但輸出開不了或啟動失敗；種類與 DSD 包裝器不相容在下面兩欄。
        std::optional<Error> startError;
        OutputErrorKind errorKind = OutputErrorKind::Other;
        bool dsdRejected = false;
        quint64 token = 0;
        AudioFormat outputFormat;
    };

    /// 開檔並登記 token；可以在任何執行緒呼叫。
    Result<EngineTrack> openTrack(const QueueEntry& entry, qint64 offsetMs);
    std::optional<TokenInfo> tokenInfo(quint64 token) const;
    /// 從 entry 的 offsetMs 開始播（送出要求，不等）；開不了的跳到下一首（AC-01-3）。
    /// 沒有能播的就停止。
    void startEntry(std::optional<QueueEntry> entry, qint64 offsetMs = 0);
    /// startEntry 與跳過開不了的曲目時用：照 m_startTries 限制最多試整個佇列一輪。
    void requestStart(std::optional<QueueEntry> entry, qint64 offsetMs);
    /// 把要求交給背景執行緒（取代還沒開始的那個）；之後引擎歸背景執行緒，直到結果回來。
    void postJob(StartJob job);
    /// 背景執行緒：一個一個做要求。
    void starterLoop();
    StartResult runJob(StartJob job);
    /// UI 執行緒：處理背景交回的結果（舊序號的丟掉）。
    void drainStartResults();
    void finishStart(const StartResult& result);
    /// 開不了或裝置不支援：記下、接著試下一首。
    void skipAndContinue(const QueueEntry& entry, const QString& reason);
    /// 記下開不了的曲目並通知（AC-01-3）。
    void markUnplayable(const QueueEntry& entry, const QString& reason);
    IOutputBackend* ensureBackend();
    void onTokenChanged(quint64 token);
    void onFinished();
    void stopEngine();
    void stopWithError(const QString& error);
    /// 裝置錯誤給使用者看的原因（AC-02-4）；技術細節寫進日誌。
    QString deviceErrorText(OutputErrorKind kind, const Error& error) const;
    void setState(State state);
    void setPosition(qint64 positionMs);
    void updateOutputStatus();
    /// 目前曲目自動接續的下一首（沒有時 0）。
    quint64 upcomingId() const;
    /// 佇列改變後：自動接續的下一首變了就撤回引擎已預開的那首。
    void afterQueueChange(quint64 upcomingBefore);

    SourceRegistryImpl& m_sources;
    OutputRegistryImpl& m_outputs;
    PlaybackQueue m_queue;
    std::unique_ptr<IOutputBackend> m_backend;
    OutputChoice m_choice;

    mutable std::mutex m_tokenMutex;
    std::unordered_map<quint64, TokenInfo> m_tokens;
    quint64 m_nextToken = 1;

    // 以下只在 UI 執行緒
    State m_state = State::Stopped;
    quint64 m_token = 0;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = -1;
    qint64 m_sourceRate = 0;
    qint64 m_outputRate = 0;
    QString m_outputError;
    OutputStatus m_output;
    /// m_output.reasons 最後一項是不是音量的原因（暫停中改音量時換掉它）。
    bool m_outputHasVolume = false;
    /// 目前的曲目能不能調音量（DSD 直通時不能）；停止時回到 true。
    bool m_volumeAvailable = true;
    QTimer m_timer;
    /// 有要求還沒回來：引擎在背景執行緒手上，poll() 與輸出狀態不讀它。
    bool m_starting = false;
    /// 還沒回來的開始播放要求的曲目與位置（只停止的要求沒有）；上下首、跳轉、暫停以它為準。
    std::optional<QueueEntry> m_pendingEntry;
    qint64 m_pendingOffset = 0;
    /// 這次開始還能試幾首（開不了的一首一首跳過，最多整個佇列一輪）。
    qsizetype m_startTries = 0;

    // 背景執行緒與 UI 執行緒交接要求與結果（m_startMutex 保護）
    std::mutex m_startMutex;
    std::condition_variable m_startWake;
    std::condition_variable m_startIdle;
    std::optional<StartJob> m_job;
    bool m_jobRunning = false;
    bool m_quit = false;
    std::deque<StartResult> m_results;
    /// 最新要求的序號；背景執行緒看到不同就放棄手上的要求。
    std::atomic<quint64> m_startSeq{0};
    std::thread m_starter;

    // 最後宣告、最先解構：引擎停下來之後才拆掉它會呼叫的佇列與 token 表
    PlaybackEngine m_engine;
};

} // namespace auralis::core
