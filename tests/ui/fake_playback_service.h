#pragma once

// 介面測試用的 PlaybackService：只在記憶體裡照介面說明的規則改佇列與狀態，不開檔、不發聲。
// 每個操作記在 calls（例："move 3 1"），測試以此確認畫面呼叫了哪個方法。

#include <auralis/playback_service.h>

#include <QStringList>

#include <algorithm>

namespace auralis::test {

class FakePlaybackService final : public PlaybackService {
    Q_OBJECT
    Q_PROPERTY(QStringList calls READ calls NOTIFY recorded)
    Q_PROPERTY(QStringList paths READ paths NOTIFY queueChanged)
    Q_PROPERTY(int current READ currentIndex NOTIFY queueChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY recorded)

public:
    /// 佇列換成 count 首（路徑 C:/t/<n>.flac），目前曲目是 current（-1 表示沒有），停止中。
    Q_INVOKABLE void reset(int count, int current) {
        m_entries.clear();
        for (int i = 0; i < count; ++i) {
            append({QStringLiteral("C:/t/%1.flac").arg(i), 0});
        }
        m_current = current;
        m_calls.clear();
        m_volume = 100;
        emit volumeChanged(m_volume);
        setVolumeAvailable(true);
        setState(State::Stopped);
        emit queueChanged();
        emit trackChanged();
        emit recorded();
    }
    /// 同 reset，但停在暫停（有目前曲目時播放列才顯示曲名與進度條；已停止時收起，AC-02-8）。
    Q_INVOKABLE void resetPaused(int count, int current) {
        reset(count, current);
        setState(State::Paused);
    }
    QStringList calls() const { return m_calls; }
    QStringList paths() const {
        QStringList result;
        for (const auto& entry : m_entries) {
            result.append(entry.track.path);
        }
        return result;
    }
    bool isPlaying() const { return m_state == State::Playing; }

    void playFrom(const QList<TrackRef>& tracks, int index) override {
        m_entries.clear();
        for (const auto& track : tracks) {
            append(track);
        }
        record(QStringLiteral("playFrom %1").arg(index));
        m_current = index;
        emit queueChanged();
    }
    void enqueueNext(const QList<TrackRef>& tracks) override {
        int at = m_current + 1;
        for (const auto& track : tracks) {
            m_entries.insert(at++, {m_nextId++, track, false});
        }
        record(QStringLiteral("enqueueNext %1").arg(tracks.size()));
        emit queueChanged();
    }
    void enqueueLast(const QList<TrackRef>& tracks) override {
        for (const auto& track : tracks) {
            append(track);
        }
        record(QStringLiteral("enqueueLast %1").arg(tracks.size()));
        emit queueChanged();
    }
    void playAt(int index) override {
        record(QStringLiteral("playAt %1").arg(index));
        m_current = index;
        setState(State::Playing);
        emit queueChanged();
        emit trackChanged();
    }
    void remove(const QList<quint64>& ids) override {
        for (const quint64 id : ids) {
            const auto found = std::find_if(m_entries.begin(), m_entries.end(),
                                            [id](const QueueEntry& e) { return e.id == id; });
            if (found != m_entries.end()) {
                const int at = static_cast<int>(found - m_entries.begin());
                m_entries.erase(found);
                if (at < m_current) {
                    --m_current;
                }
                record(QStringLiteral("remove %1").arg(at));
            }
        }
        emit queueChanged();
    }
    void move(int from, int to) override {
        const quint64 currentId = m_current >= 0 ? m_entries.at(m_current).id : 0;
        m_entries.move(from, to);
        if (currentId != 0) {
            for (int i = 0; i < m_entries.size(); ++i) {
                if (m_entries.at(i).id == currentId) {
                    m_current = i;
                }
            }
        }
        record(QStringLiteral("move %1 %2").arg(from).arg(to));
        emit queueChanged();
    }
    void clear() override {
        if (m_current >= 0) {
            m_entries = {m_entries.at(m_current)};
            m_current = 0;
        } else {
            m_entries.clear();
        }
        record(QStringLiteral("clear"));
        emit queueChanged();
    }
    QList<QueueEntry> queue() const override { return m_entries; }
    int currentIndex() const override { return m_current; }
    std::optional<QueueEntry> currentEntry() const override {
        if (m_current < 0 || m_current >= m_entries.size()) {
            return std::nullopt;
        }
        return m_entries.at(m_current);
    }

    void play() override {
        record(QStringLiteral("play"));
        if (m_current < 0 && !m_entries.isEmpty()) {
            m_current = 0;
            emit queueChanged();
            emit trackChanged();
        }
        setState(State::Playing);
    }
    void pause() override {
        record(QStringLiteral("pause"));
        setState(State::Paused);
    }
    void togglePause() override {
        record(QStringLiteral("togglePause"));
        setState(m_state == State::Playing ? State::Paused : State::Playing);
    }
    void stop() override {
        record(QStringLiteral("stop"));
        setState(State::Stopped);
        m_position = 0;
        emit positionChanged(0);
    }
    void next() override {
        record(QStringLiteral("next"));
        if (m_current + 1 < m_entries.size()) {
            ++m_current;
            emit queueChanged();
            emit trackChanged();
        }
    }
    void previous() override {
        record(QStringLiteral("previous"));
        if (m_current > 0) {
            --m_current;
            emit queueChanged();
            emit trackChanged();
        }
    }
    void seek(qint64 positionMs) override {
        record(QStringLiteral("seek %1").arg(positionMs));
        m_position = positionMs;
        emit positionChanged(positionMs);
    }

    /// 模擬服務每 50 毫秒輪詢送來的播放位置（不記進 calls）。
    Q_INVOKABLE void tick(qint64 positionMs) {
        m_position = positionMs;
        emit positionChanged(positionMs);
    }

    State state() const override { return m_state; }
    qint64 positionMs() const override { return m_position; }
    qint64 durationMs() const override { return m_current >= 0 ? 200000 : -1; }
    RepeatMode repeatMode() const override { return m_repeat; }
    void setRepeatMode(RepeatMode mode) override {
        m_repeat = mode;
        emit repeatModeChanged(mode);
    }
    bool shuffle() const override { return m_shuffle; }
    void setShuffle(bool on) override {
        m_shuffle = on;
        emit shuffleChanged(on);
    }
    OutputStatus outputStatus() const override { return m_output; }

    int volume() const override { return m_volume; }
    void setVolume(int percent) override {
        m_volume = std::clamp(percent, 0, 100);
        record(QStringLiteral("setVolume %1").arg(m_volume));
        emit volumeChanged(m_volume);
    }
    bool volumeAvailable() const override { return m_volumeAvailable; }
    QString volumeUnavailableReason() const override {
        return m_volumeAvailable ? QString() : QStringLiteral("DSD 直通時無法調整音量");
    }
    /// 模擬 DSD 直通播放中（音量不能調）。
    Q_INVOKABLE void setVolumeAvailable(bool available) {
        if (m_volumeAvailable != available) {
            m_volumeAvailable = available;
            emit volumeAvailableChanged(available);
        }
    }

    /// 輸出狀態：backend 空字串表示沒有在播；有時來源與裝置都是 96 kHz / 24 bit。
    Q_INVOKABLE void setOutput(const QString& backend, const QStringList& reasons,
                               const QString& error) {
        OutputStatus status;
        status.backend = backend;
        if (!backend.isEmpty()) {
            status.sourceFormat = {96000, 24, 32, 2, false, 0};
            status.deviceFormat = status.sourceFormat;
        }
        status.bitPerfect = reasons.isEmpty();
        status.reasons = reasons;
        status.error = error;
        setOutputStatus(status);
    }
    void setOutputStatus(const OutputStatus& status) {
        m_output = status;
        emit outputStatusChanged(status);
    }
    /// 模擬開不了某一首（AC-01-3）。
    Q_INVOKABLE void skip(const QString& path, const QString& reason) {
        emit trackSkipped(TrackRef{path, 0}, reason);
    }

signals:
    void recorded();

private:
    void append(const TrackRef& track) { m_entries.append({m_nextId++, track, false}); }
    void record(const QString& call) {
        m_calls.append(call);
        emit recorded();
    }
    void setState(State state) {
        if (m_state != state) {
            m_state = state;
            emit stateChanged(state);
        }
    }

    QList<QueueEntry> m_entries;
    int m_current = -1;
    quint64 m_nextId = 1;
    State m_state = State::Stopped;
    qint64 m_position = 0;
    RepeatMode m_repeat = RepeatMode::Off;
    bool m_shuffle = false;
    int m_volume = 100;
    bool m_volumeAvailable = true;
    QStringList m_calls;
    OutputStatus m_output;
};

} // namespace auralis::test
