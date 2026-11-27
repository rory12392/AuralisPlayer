// PlaybackService 的佇列操作（AC-11-2）：取代、接著播放、加到最後、移出、排序、清空、隨機、重複。
// 佇列在引擎預開下一首之後改變時，撤回引擎手上的那首（PlaybackEngine::invalidateNextTrack），
// 讓無縫接續照新的佇列走。
#include "playback/playback_service_impl.h"

#include <unordered_set>

namespace auralis::core {

quint64 PlaybackServiceImpl::upcomingId() const {
    // 還在開檔時，引擎預開的是要開的那首之後的那首
    const auto current = m_pendingEntry ? m_pendingEntry : m_queue.current();
    const auto next = current ? m_queue.nextAfter(current->id, true) : std::nullopt;
    return next ? next->id : 0;
}

void PlaybackServiceImpl::afterQueueChange(quint64 upcomingBefore) {
    emit queueChanged();
    // 開始中（含從停止或暫停開始）預填可能已經照舊佇列預開了下一首
    if ((m_state == State::Playing || m_pendingEntry) && upcomingId() != upcomingBefore) {
        m_engine.invalidateNextTrack();
    }
}

void PlaybackServiceImpl::playFrom(const QList<TrackRef>& tracks, int index) {
    m_queue.replace(tracks, index);
    startEntry(m_queue.current());
}

void PlaybackServiceImpl::enqueueNext(const QList<TrackRef>& tracks) {
    const quint64 before = upcomingId();
    m_queue.insertNext(tracks);
    afterQueueChange(before);
}

void PlaybackServiceImpl::enqueueLast(const QList<TrackRef>& tracks) {
    const quint64 before = upcomingId();
    m_queue.append(tracks);
    afterQueueChange(before);
}

void PlaybackServiceImpl::playAt(int index) {
    const auto entry = m_queue.entryAt(index);
    if (!entry) {
        return;
    }
    // 使用者明確選了它：之前開不了的也再試一次
    m_queue.setUnplayable(entry->id, false);
    startEntry(m_queue.entry(entry->id));
}

void PlaybackServiceImpl::remove(const QList<quint64>& ids) {
    const quint64 before = upcomingId();
    // 還在開檔時，要開的那首就是「正在播的」
    const auto current = m_pendingEntry ? m_pendingEntry : m_queue.current();
    const std::unordered_set<quint64> doomed(ids.begin(), ids.end());
    const bool removesCurrent = current && doomed.contains(current->id);
    // 移掉正在播的那首時接著播它後面第一首沒被移掉的
    std::optional<QueueEntry> successor;
    if (removesCurrent) {
        successor = m_queue.nextAfter(current->id, false);
        for (qsizetype tries = m_queue.entries().size();
             successor && doomed.contains(successor->id) && tries > 0; --tries) {
            successor = m_queue.nextAfter(successor->id, false);
        }
        if (successor && doomed.contains(successor->id)) {
            successor.reset();
        }
    }
    if (m_queue.remove(ids) == 0) {
        return;
    }
    if (!removesCurrent) {
        afterQueueChange(before);
        return;
    }
    if ((m_state != State::Stopped || m_pendingEntry) && successor) {
        startEntry(m_queue.entry(successor->id));
        return;
    }
    stop();
    emit trackChanged();
    emit queueChanged();
}

void PlaybackServiceImpl::move(int from, int to) {
    const quint64 before = upcomingId();
    if (m_queue.move(from, to)) {
        afterQueueChange(before);
    }
}

void PlaybackServiceImpl::clear() {
    const quint64 before = upcomingId();
    // 還在開檔時，要開的那首就是「正在播的」：留下它（開好時本來就會成為目前曲目）
    if (m_pendingEntry && m_queue.entry(m_pendingEntry->id)) {
        m_queue.setCurrent(m_pendingEntry->id);
    }
    m_queue.clearExceptCurrent();
    afterQueueChange(before);
}

void PlaybackServiceImpl::setRepeatMode(RepeatMode mode) {
    if (mode == m_queue.repeat()) {
        return;
    }
    const quint64 before = upcomingId();
    m_queue.setRepeat(mode);
    emit repeatModeChanged(mode);
    afterQueueChange(before);
}

void PlaybackServiceImpl::setShuffle(bool on) {
    if (on == m_queue.shuffle()) {
        return;
    }
    const quint64 before = upcomingId();
    m_queue.setShuffle(on);
    emit shuffleChanged(on);
    afterQueueChange(before);
}

} // namespace auralis::core
