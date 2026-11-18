#include "playback/playback_queue.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace auralis::core {

PlaybackQueue::PlaybackQueue(quint32 seed) : m_random(seed) {}

std::vector<QueueEntry> PlaybackQueue::makeEntries(const QList<TrackRef>& tracks) {
    std::vector<QueueEntry> entries;
    entries.reserve(static_cast<std::size_t>(tracks.size()));
    for (const TrackRef& track : tracks) {
        entries.push_back(QueueEntry{m_nextId++, track, false});
    }
    return entries;
}

int PlaybackQueue::indexOfLocked(quint64 id) const {
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PlaybackQueue::shuffleAfter(int from) {
    const auto begin = m_entries.begin() + (from + 1);
    if (begin < m_entries.end()) {
        std::shuffle(begin, m_entries.end(), m_random);
    }
}

void PlaybackQueue::replace(const QList<TrackRef>& tracks, int start) {
    const std::lock_guard lock(m_mutex);
    m_entries = makeEntries(tracks);
    const bool valid = start >= 0 && start < static_cast<int>(m_entries.size());
    m_currentId = valid ? m_entries[static_cast<std::size_t>(start)].id : 0;
    m_unshuffled.clear();
    if (!m_shuffle) {
        return;
    }
    for (const QueueEntry& entry : m_entries) {
        m_unshuffled.push_back(entry.id);
    }
    if (valid) {
        // 選的那首先播，其餘打亂
        std::rotate(m_entries.begin(), m_entries.begin() + start, m_entries.begin() + start + 1);
        shuffleAfter(0);
    } else {
        shuffleAfter(-1);
    }
}

void PlaybackQueue::insertNext(const QList<TrackRef>& tracks) {
    const std::lock_guard lock(m_mutex);
    std::vector<QueueEntry> added = makeEntries(tracks);
    const int at = indexOfLocked(m_currentId) + 1;
    if (m_shuffle) {
        const auto current = std::find(m_unshuffled.begin(), m_unshuffled.end(), m_currentId);
        auto pos = current == m_unshuffled.end() ? m_unshuffled.begin() : current + 1;
        for (const QueueEntry& entry : added) {
            pos = m_unshuffled.insert(pos, entry.id) + 1;
        }
    }
    m_entries.insert(m_entries.begin() + at, added.begin(), added.end());
}

void PlaybackQueue::append(const QList<TrackRef>& tracks) {
    const std::lock_guard lock(m_mutex);
    for (QueueEntry& entry : makeEntries(tracks)) {
        if (m_shuffle) {
            m_unshuffled.push_back(entry.id);
        }
        m_entries.push_back(std::move(entry));
    }
}

int PlaybackQueue::remove(const QList<quint64>& ids) {
    const std::lock_guard lock(m_mutex);
    const std::unordered_set<quint64> doomed(ids.begin(), ids.end());
    const auto before = m_entries.size();
    std::erase_if(m_entries, [&](const QueueEntry& entry) { return doomed.contains(entry.id); });
    std::erase_if(m_unshuffled, [&](quint64 id) { return doomed.contains(id); });
    if (doomed.contains(m_currentId)) {
        m_currentId = 0;
    }
    return static_cast<int>(before - m_entries.size());
}

bool PlaybackQueue::move(int from, int to) {
    const std::lock_guard lock(m_mutex);
    const int size = static_cast<int>(m_entries.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
        return false;
    }
    const auto first = m_entries.begin();
    if (from < to) {
        std::rotate(first + from, first + from + 1, first + to + 1);
    } else {
        std::rotate(first + to, first + from, first + from + 1);
    }
    return true;
}

void PlaybackQueue::clearExceptCurrent() {
    const std::lock_guard lock(m_mutex);
    std::erase_if(m_entries, [this](const QueueEntry& entry) { return entry.id != m_currentId; });
    std::erase_if(m_unshuffled, [this](quint64 id) { return id != m_currentId; });
}

void PlaybackQueue::setShuffle(bool on) {
    const std::lock_guard lock(m_mutex);
    if (on == m_shuffle) {
        return;
    }
    m_shuffle = on;
    if (on) {
        m_unshuffled.clear();
        for (const QueueEntry& entry : m_entries) {
            m_unshuffled.push_back(entry.id);
        }
        shuffleAfter(indexOfLocked(m_currentId));
        return;
    }
    // 回到打亂前的順序；隨機播放時加入的項目已經照「接著播放／加到最後」記在原本的順序裡
    std::unordered_map<quint64, std::size_t> rank;
    for (std::size_t i = 0; i < m_unshuffled.size(); ++i) {
        rank.emplace(m_unshuffled[i], i);
    }
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [&](const QueueEntry& a, const QueueEntry& b) {
                         const auto ra = rank.find(a.id);
                         const auto rb = rank.find(b.id);
                         const std::size_t ka = ra == rank.end() ? rank.size() : ra->second;
                         const std::size_t kb = rb == rank.end() ? rank.size() : rb->second;
                         return ka < kb;
                     });
    m_unshuffled.clear();
}

bool PlaybackQueue::shuffle() const {
    const std::lock_guard lock(m_mutex);
    return m_shuffle;
}

void PlaybackQueue::setRepeat(RepeatMode mode) {
    const std::lock_guard lock(m_mutex);
    m_repeat = mode;
}

PlaybackQueue::RepeatMode PlaybackQueue::repeat() const {
    const std::lock_guard lock(m_mutex);
    return m_repeat;
}

QList<QueueEntry> PlaybackQueue::entries() const {
    const std::lock_guard lock(m_mutex);
    return QList<QueueEntry>(m_entries.begin(), m_entries.end());
}

std::optional<QueueEntry> PlaybackQueue::entry(quint64 id) const {
    const std::lock_guard lock(m_mutex);
    const int index = indexOfLocked(id);
    if (index < 0) {
        return std::nullopt;
    }
    return m_entries[static_cast<std::size_t>(index)];
}

std::optional<QueueEntry> PlaybackQueue::entryAt(int index) const {
    const std::lock_guard lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_entries.size())) {
        return std::nullopt;
    }
    return m_entries[static_cast<std::size_t>(index)];
}

int PlaybackQueue::indexOf(quint64 id) const {
    const std::lock_guard lock(m_mutex);
    return indexOfLocked(id);
}

std::optional<QueueEntry> PlaybackQueue::current() const {
    const std::lock_guard lock(m_mutex);
    const int index = indexOfLocked(m_currentId);
    if (index < 0) {
        return std::nullopt;
    }
    return m_entries[static_cast<std::size_t>(index)];
}

int PlaybackQueue::currentIndex() const {
    const std::lock_guard lock(m_mutex);
    return indexOfLocked(m_currentId);
}

void PlaybackQueue::setCurrent(quint64 id) {
    const std::lock_guard lock(m_mutex);
    m_currentId = indexOfLocked(id) >= 0 ? id : 0;
}

void PlaybackQueue::setUnplayable(quint64 id, bool unplayable) {
    const std::lock_guard lock(m_mutex);
    const int index = indexOfLocked(id);
    if (index >= 0) {
        m_entries[static_cast<std::size_t>(index)].unplayable = unplayable;
    }
}

std::optional<QueueEntry> PlaybackQueue::nextAfter(quint64 id, bool automatic) const {
    const std::lock_guard lock(m_mutex);
    const int index = indexOfLocked(id);
    if (index < 0) {
        return std::nullopt;
    }
    const auto at = [this](int i) -> const QueueEntry& {
        return m_entries[static_cast<std::size_t>(i)];
    };
    if (automatic && m_repeat == RepeatMode::One) {
        return at(index).unplayable ? std::nullopt : std::optional<QueueEntry>(at(index));
    }
    const int size = static_cast<int>(m_entries.size());
    for (int i = index + 1; i < size; ++i) {
        if (!at(i).unplayable) {
            return at(i);
        }
    }
    if (m_repeat == RepeatMode::All) {
        for (int i = 0; i <= index; ++i) {
            if (!at(i).unplayable) {
                return at(i);
            }
        }
    }
    return std::nullopt;
}

std::optional<QueueEntry> PlaybackQueue::previousBefore(quint64 id) const {
    const std::lock_guard lock(m_mutex);
    const int index = indexOfLocked(id);
    if (index < 0) {
        return std::nullopt;
    }
    for (int i = index - 1; i >= 0; --i) {
        if (!m_entries[static_cast<std::size_t>(i)].unplayable) {
            return m_entries[static_cast<std::size_t>(i)];
        }
    }
    if (m_repeat == RepeatMode::All) {
        for (int i = static_cast<int>(m_entries.size()) - 1; i > index; --i) {
            if (!m_entries[static_cast<std::size_t>(i)].unplayable) {
                return m_entries[static_cast<std::size_t>(i)];
            }
        }
    }
    return std::nullopt;
}

} // namespace auralis::core
