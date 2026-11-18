#pragma once

#include "audio/next_track.h"
#include "audio/output_registry_impl.h"
#include "audio/playback_engine.h"
#include "audio/processor_chain_impl.h"
#include "audio/source_registry_impl.h"
#include "playback/playback_queue.h"

#include <auralis/playback_service.h>

#include <QTimer>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace auralis::core {

/// 毫秒與樣本框換算用的每秒樣本框數（PlaybackService 的幾個實作檔共用）。
inline qint64 framesPerSecond(const AudioFormat& format) {
    return format.rate > 0 ? format.rate : format.dsdRate / 32;
}

/// PlaybackService 的實作：佇列（PlaybackQueue）＋播放引擎＋下一首提供者。
/// UI 執行緒擁有；引擎在解碼執行緒向它要下一首（nextTrackAfter），那裡只讀佇列、開檔、記 token。
/// 正在聽哪一首、播完沒、裝置錯誤都由 50 毫秒的計時器輪詢引擎得知（poll）。
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

    /// 開檔並登記 token；可以在任何執行緒呼叫。
    Result<EngineTrack> openTrack(const QueueEntry& entry, qint64 offsetMs);
    std::optional<TokenInfo> tokenInfo(quint64 token) const;
    /// 從 entry 的 offsetMs 開始播；開不了的跳到下一首（AC-01-3）。沒有能播的就停止。
    bool startEntry(std::optional<QueueEntry> entry, qint64 offsetMs = 0);
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

    // 最後宣告、最先解構：引擎停下來之後才拆掉它會呼叫的佇列與 token 表
    PlaybackEngine m_engine;
};

} // namespace auralis::core
