#pragma once

#include <auralis/services.h>

#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace auralis {

/// 佇列裡的一個項目。同一首可以在佇列出現多次，用 id 區分。值型別。
struct QueueEntry {
    /// 佇列內唯一、不重用的編號（從 1 開始）。
    quint64 id = 0;
    TrackRef track;
    /// 這次開不了（AC-01-3）：自動接續時跳過；使用者明確選它（playAt）時再試一次。
    bool unplayable = false;
};

/// 播放佇列與傳輸控制（US-11 AC-11-2），加上播放狀態的訂閱。
/// 佇列的順序就是播放順序：隨機播放打開時，目前曲目之後的項目被打亂；關掉時回到原本的順序。
/// 重複：Off 播完最後一首停止；All 播完最後一首回到第一首；One 自動接續時重播同一首
/// （按「下一首」仍然換下一首）。
/// 執行緒：只在 UI 執行緒呼叫，訊號也在 UI 執行緒發出。核心擁有實例，模組經 ModuleHost::playback()
/// 取得參考。
class PlaybackService : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY trackChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY queueChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY volumeAvailableChanged)

public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)
    enum class RepeatMode { Off, All, One };
    Q_ENUM(RepeatMode)

    ~PlaybackService() override = default;
    PlaybackService(const PlaybackService&) = delete;
    PlaybackService& operator=(const PlaybackService&) = delete;

    // ---- 佇列 ----

    /// 以 tracks 取代整個佇列，從 index 那首開始播（隨機播放時 index 那首先播，其餘打亂）。
    virtual void playFrom(const QList<TrackRef>& tracks, int index) = 0;
    /// 接著播放：插在目前曲目之後（沒有目前曲目時插在最前面），保持 tracks 的順序。
    virtual void enqueueNext(const QList<TrackRef>& tracks) = 0;
    /// 加到佇列最後。
    virtual void enqueueLast(const QList<TrackRef>& tracks) = 0;
    /// 跳到佇列的第 index 項開始播（佇列面板雙擊）。
    Q_INVOKABLE virtual void playAt(int index) = 0;
    /// 移出佇列（不動檔案）。移掉正在播的那首時接著播它後面那首（沒有就停止）。
    virtual void remove(const QList<quint64>& ids) = 0;
    /// 把第 from 項移到第 to 項（拖曳排序）。
    Q_INVOKABLE virtual void move(int from, int to) = 0;
    /// 清空佇列：正在播的那首留著繼續播，其他項目都移出。
    Q_INVOKABLE virtual void clear() = 0;
    /// 佇列內容（播放順序）。
    virtual QList<QueueEntry> queue() const = 0;
    /// 目前曲目在佇列的位置；沒有時 -1。
    virtual int currentIndex() const = 0;
    /// 目前曲目；沒有時 std::nullopt。
    virtual std::optional<QueueEntry> currentEntry() const = 0;

    // ---- 傳輸 ----

    /// 暫停中從暫停的位置繼續；停止時播目前曲目（沒有就播佇列第一首）。
    Q_INVOKABLE virtual void play() = 0;
    Q_INVOKABLE virtual void pause() = 0;
    Q_INVOKABLE virtual void togglePause() = 0;
    Q_INVOKABLE virtual void stop() = 0;
    /// 下一首（照隨機與重複全部；最後一首而且重複關閉時什麼都不做）。
    Q_INVOKABLE virtual void next() = 0;
    /// 播超過 3 秒時回到開頭，否則上一首。
    Q_INVOKABLE virtual void previous() = 0;
    /// 跳到目前曲目的 positionMs 毫秒（暫停中只改位置）。
    Q_INVOKABLE virtual void seek(qint64 positionMs) = 0;

    virtual State state() const = 0;
    /// 目前曲目的位置與長度（毫秒）；長度不知道時 -1。
    virtual qint64 positionMs() const = 0;
    virtual qint64 durationMs() const = 0;
    virtual RepeatMode repeatMode() const = 0;
    virtual void setRepeatMode(RepeatMode mode) = 0;
    virtual bool shuffle() const = 0;
    virtual void setShuffle(bool on) = 0;
    virtual OutputStatus outputStatus() const = 0;

    // ---- 音量（AC-02-7） ----

    /// 音量百分比，0–100，預設 100；核心記住，重新啟動後沿用。
    virtual int volume() const = 0;
    /// 設定音量（超出 0–100 的值取到範圍內）。100 時不處理訊號（位元完美）；其他值時輸出狀態
    /// 變成非位元完美，原因多一項「音量 n%」。百分比對應的增益是三次曲線 (n/100)³，0 是靜音。
    /// 播放中立即生效（約 0.1 秒多聽得到），增益平滑過渡，不重開裝置。
    /// volumeAvailable() 為 false 時照樣記下，等能調的曲目才作用。
    Q_INVOKABLE virtual void setVolume(int percent) = 0;
    /// 目前能不能調音量：DoP 與原生 DSD 直通播放中是 false（位元流不能乘上增益）；
    /// 沒有在播時是 true。
    virtual bool volumeAvailable() const = 0;
    /// 不能調的原因，給使用者看（例：「DSD 直通時無法調整音量」）；能調時為空。
    virtual QString volumeUnavailableReason() const = 0;

signals:
    /// 正在聽的曲目換了（自動接續、跳到別首、重複單曲重播）；讀 currentEntry()。
    void trackChanged();
    /// 播放中每 50 毫秒一次，跳轉時也發。
    void positionChanged(qint64 positionMs);
    void stateChanged(auralis::PlaybackService::State state);
    /// 輸出路徑、格式、位元完美與原因、裝置錯誤改變時。
    void outputStatusChanged(const auralis::OutputStatus& status);
    /// 要換輸出路徑了（backend 是輸出後端工廠的名稱，例：WASAPI 共享）：在新輸出開啟、
    /// 曲目從原位置繼續之前，於 UI 執行緒同步發出（播放中換輸出、第一次播放選預設輸出、
    /// 新輸出開不了而換回原本的輸出時都會發）。給進階輸出模組在開新輸出前換好 DSD 包裝器：
    /// 收到時呼叫 OutputRegistry::setDsdSink，接著開的輸出就用新的包裝器。
    /// 連接時用預設（直接）連線；處理時不可呼叫 play()、pause()、stop() 等會重入播放的方法。
    void outputPathChanging(const QString& backend);
    /// 佇列內容、順序或目前位置改變時。
    void queueChanged();
    void repeatModeChanged(auralis::PlaybackService::RepeatMode mode);
    void shuffleChanged(bool on);
    void volumeChanged(int percent);
    /// 能不能調音量改變時（開始或離開 DSD 直通）；原因讀 volumeUnavailableReason()。
    void volumeAvailableChanged(bool available);
    /// 開不了某一首、已跳到下一首（AC-01-3）。reason 是給使用者看的原因。
    void trackSkipped(const auralis::TrackRef& track, const QString& reason);

protected:
    explicit PlaybackService(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace auralis
