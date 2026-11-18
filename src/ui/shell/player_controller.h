#pragma once

#include "shell/queue_model.h"

#include <auralis/library_query.h>
#include <auralis/playback_service.h>

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

class QQmlEngine;
class QJSEngine;

namespace auralis::ui {

/// 正在播放面板（S-11）與播放列封面要的目前曲目資訊。
struct NowPlayingInfo {
    QString album;
    /// 音樂庫的專輯 id（點專輯開專輯詳細頁）；不屬於任何專輯時 0。
    qint64 albumId = 0;
    int year = 0;
    int trackNo = 0;
    /// 同一張專輯的曲目數；不知道時 0。
    int trackTotal = 0;
    QString codec;
    /// DSD 是 DSD 率。
    int sampleRate = 0;
    /// 有損格式是 0。
    int bits = 0;
    /// PCM 是 0。
    int dsdRate = 0;
    /// 平均位元率；不知道時 -1。
    int bitrateKbps = -1;
    /// 封面的 key（CoverService），空字串＝沒有封面。
    QString coverKey;
};

/// 依 TrackRef 查目前曲目的資訊（主程式接 LibraryQuery）。在工作執行緒呼叫；
/// 音樂庫裡沒有這首時回傳 std::nullopt。
using NowPlayingLookup = std::function<std::optional<NowPlayingInfo>(const TrackRef&)>;

/// 音樂庫的曲目資訊換成面板的資訊。fileBytes 是音樂檔的大小（不知道時 -1），
/// 位元率是整個檔案的平均值（檔案大小 × 8 ÷ 長度）。任何執行緒。
NowPlayingInfo nowPlayingFrom(const TrackInfo& track, int trackTotal, qint64 fileBytes);

/// 播放列（S-02）、佇列面板（S-13）、正在播放面板（S-11）用的 PlaybackService：
/// QML 以單例 Player 取用。
/// 把服務的狀態換成畫面直接能用的屬性（是否在播、曲名與藝人、重複模式的文字、封面與格式讀數），
/// 傳輸操作直接轉給服務。主程式在建立 QML 引擎前 setInstance。
/// 執行緒：只在 UI 執行緒使用；目前曲目的資訊在自己的工作執行緒查。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PlayerController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Player)
    QML_SINGLETON
    Q_PROPERTY(auralis::ui::QueueModel* queue READ queue CONSTANT)
    /// 有目前曲目（播放中、暫停或停在某一首）。
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    /// 佇列有曲目：傳輸鍵可以用。
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    /// 已停止（AC-02-8）：有目前曲目、停止中、不是裝置錯誤停下來的（使用者按停止、SMTC 停止、
    /// 佇列播完）。這時播放列回到「尚未播放」，不顯示曲名與進度條；裝置錯誤時仍顯示曲名與原因。
    Q_PROPERTY(bool stopped READ stopped NOTIFY stoppedChanged)
    /// 播放中或暫停：「停止」可以按。
    Q_PROPERTY(bool canStop READ canStop NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    /// 不知道時 -1。
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    /// off、all、one。
    Q_PROPERTY(QString repeatMode READ repeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY shuffleChanged)
    /// 音量百分比 0–100（AC-02-7）。
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    /// DSD 直通播放中不能調音量；原因給滑桿的 ToolTip。
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY volumeAvailableChanged)
    Q_PROPERTY(
        QString volumeUnavailableReason READ volumeUnavailableReason NOTIFY volumeAvailableChanged)
    /// 目前曲目的封面（image://cover/<key>）；沒有封面或還沒查到時空字串。
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString album READ album NOTIFY nowPlayingChanged)
    /// 目前曲目的專輯 id（正在播放面板點專輯跳頁，S-11）；不在音樂庫的專輯裡或還沒查到時 0。
    Q_PROPERTY(qint64 albumId READ albumId NOTIFY nowPlayingChanged)
    /// 「<專輯>　<年份>」，缺的那一半省略。
    Q_PROPERTY(QString albumLine READ albumLine NOTIFY nowPlayingChanged)
    /// 「曲目 <n> / <總數>」；不知道總數時「曲目 <n>」，沒有軌號時空字串。
    Q_PROPERTY(QString trackText READ trackText NOTIFY nowPlayingChanged)
    /// 四格讀數，每格 {label, value, unit}：格式、取樣率、位元深度、位元率；
    /// DSD 時是格式、DSD 倍率、輸出方式、位元率。不知道的值寫「—」。
    Q_PROPERTY(QVariantList readouts READ readouts NOTIFY nowPlayingChanged)

public:
    /// lookup 為空時沒有目前曲目的資訊（測試用）。
    PlayerController(PlaybackService& service, QueueModel& queue, NowPlayingLookup lookup = {},
                     QObject* parent = nullptr);
    ~PlayerController() override;

    QueueModel* queue() const { return &m_queue; }
    bool hasTrack() const { return m_service.currentEntry().has_value(); }
    bool canPlay() const { return !m_service.queue().isEmpty(); }
    bool playing() const { return m_service.state() == PlaybackService::State::Playing; }
    bool stopped() const;
    bool canStop() const { return m_service.state() != PlaybackService::State::Stopped; }
    qint64 positionMs() const { return m_service.positionMs(); }
    qint64 durationMs() const;
    QString title() const;
    QString artist() const;
    QString repeatMode() const;
    bool shuffle() const { return m_service.shuffle(); }
    int volume() const { return m_service.volume(); }
    void setVolume(int percent) { m_service.setVolume(percent); }
    bool volumeAvailable() const { return m_service.volumeAvailable(); }
    QString volumeUnavailableReason() const { return m_service.volumeUnavailableReason(); }
    QString coverUrl() const;
    QString album() const { return m_info ? m_info->album : QString(); }
    qint64 albumId() const { return m_info ? m_info->albumId : 0; }
    QString albumLine() const;
    QString trackText() const;
    QVariantList readouts() const;

    Q_INVOKABLE void togglePause();
    /// 停止並關閉輸出裝置（PlaybackService::stop）；佇列與目前曲目保留（AC-02-8）。
    Q_INVOKABLE void stop() { m_service.stop(); }
    Q_INVOKABLE void next() { m_service.next(); }
    Q_INVOKABLE void previous() { m_service.previous(); }
    Q_INVOKABLE void seek(qint64 positionMs) { m_service.seek(positionMs); }
    /// 重複：關 → 全部 → 單曲 → 關。
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void toggleShuffle() { m_service.setShuffle(!m_service.shuffle()); }
    /// 曲目右鍵選單的核心項目，json 是 TrackDragPayload 的 JSON，回傳是否接受。只帶查詢條件
    /// （超過 5,000 首）時經 QueueModel::withTracks 在工作執行緒取曲目，取到後才動佇列（T-62）。
    /// 「播放」：以這些曲目取代佇列、從第一首播（PlaybackService::playFrom(tracks, 0)）。
    Q_INVOKABLE bool playTracks(const QString& json);
    /// 「接著播放」：插在目前曲目之後（PlaybackService::enqueueNext）。
    Q_INVOKABLE bool enqueueNext(const QString& json);
    /// 「加到佇列最後」（PlaybackService::enqueueLast）。
    Q_INVOKABLE bool enqueueLast(const QString& json);
    /// 時間的寫法（介面規範 §6.1）：m:ss，超過一小時 h:mm:ss；不知道（負數）時空字串。
    Q_INVOKABLE QString formatTime(qint64 ms) const;

    /// 測試用：等目前曲目的查詢做完並套用結果。
    void waitForLookups();

    static void setInstance(PlayerController* instance);
    static PlayerController* create(QQmlEngine*, QJSEngine*);

signals:
    void trackChanged();
    void stateChanged();
    void stoppedChanged();
    void positionChanged();
    void repeatModeChanged();
    void shuffleChanged();
    void volumeChanged();
    void volumeAvailableChanged();
    void nowPlayingChanged();

private:
    /// 目前曲目換了就清掉舊資訊、在背景查新的；同一首不重查。
    void requestNowPlaying();
    /// DSD 的輸出方式（DoP、原生 DSD、轉 PCM），看目前的輸出狀態。
    QString dsdOutputMode() const;

    PlaybackService& m_service;
    QueueModel& m_queue;
    NowPlayingLookup m_lookup;
    /// 目前曲目（path＋subsong）；資訊屬於這首。
    QString m_infoKey;
    std::optional<NowPlayingInfo> m_info;
    // 最後宣告、最先解構：解構時先等背景查詢結束，結果才不會送到已拆掉的物件
    QThreadPool m_pool;
};

} // namespace auralis::ui
