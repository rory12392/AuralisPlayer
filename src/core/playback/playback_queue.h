#pragma once

#include <auralis/playback_service.h>

#include <mutex>
#include <optional>
#include <random>
#include <vector>

namespace auralis::core {

/// 播放佇列的內容與順序規則（隨機、重複）。PlaybackServiceImpl 在 UI 執行緒改它；
/// 下一首提供者在解碼執行緒讀它（nextAfter），所以每個方法都拿內部的鎖。
class PlaybackQueue {
public:
    using RepeatMode = PlaybackService::RepeatMode;

    /// seed 決定隨機播放的順序（測試用固定值）。
    explicit PlaybackQueue(quint32 seed = std::random_device{}());

    /// 以 tracks 取代全部，目前曲目是第 start 項（超出範圍時沒有目前曲目）。
    void replace(const QList<TrackRef>& tracks, int start);
    void insertNext(const QList<TrackRef>& tracks);
    void append(const QList<TrackRef>& tracks);
    /// 移出；回傳實際移掉幾項。移掉目前曲目時沒有目前曲目。
    int remove(const QList<quint64>& ids);
    bool move(int from, int to);
    /// 只留目前曲目。
    void clearExceptCurrent();

    void setShuffle(bool on);
    bool shuffle() const;
    void setRepeat(RepeatMode mode);
    RepeatMode repeat() const;

    QList<QueueEntry> entries() const;
    std::optional<QueueEntry> entry(quint64 id) const;
    std::optional<QueueEntry> entryAt(int index) const;
    int indexOf(quint64 id) const;
    std::optional<QueueEntry> current() const;
    int currentIndex() const;
    /// 設定目前曲目；id 不在佇列時沒有目前曲目。
    void setCurrent(quint64 id);
    void setUnplayable(quint64 id, bool unplayable);

    /// id 之後要播的那首：跳過開不了的；重複全部時繞回開頭。automatic 為 true 表示自動接續
    /// （重複單曲時是同一首），false 是按「下一首」。id 不在佇列時 std::nullopt。
    std::optional<QueueEntry> nextAfter(quint64 id, bool automatic) const;
    /// id 之前的那首（按「上一首」）：跳過開不了的；重複全部時繞到最後。
    std::optional<QueueEntry> previousBefore(quint64 id) const;

private:
    int indexOfLocked(quint64 id) const;
    std::vector<QueueEntry> makeEntries(const QList<TrackRef>& tracks);
    /// 把 from 之後的項目打亂。
    void shuffleAfter(int from);

    mutable std::mutex m_mutex;
    std::vector<QueueEntry> m_entries;
    /// 隨機播放打開前的順序（只在隨機播放時維護），關掉時照它排回去。
    std::vector<quint64> m_unshuffled;
    quint64 m_currentId = 0;
    quint64 m_nextId = 1;
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::Off;
    std::mt19937 m_random;
};

} // namespace auralis::core
