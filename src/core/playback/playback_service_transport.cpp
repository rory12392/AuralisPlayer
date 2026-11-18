// PlaybackService 的傳輸控制：播放、暫停、停止、上一首、下一首、跳轉。
#include "playback/playback_service_impl.h"

#include <algorithm>

namespace auralis::core {

namespace {

/// 播超過這麼久時「上一首」回到開頭。
constexpr qint64 kRestartThresholdMs = 3000;

} // namespace

void PlaybackServiceImpl::play() {
    if (m_state == State::Playing) {
        return;
    }
    const auto current = m_queue.current();
    // 暫停中，或裝置錯誤停下來之後（裝置恢復再按播放，AC-02-4）都從停下來的位置繼續
    const bool resume = m_state == State::Paused || !m_outputError.isEmpty();
    const qint64 from = resume ? m_positionMs : 0;
    startEntry(current ? current : m_queue.entryAt(0), current ? from : 0);
}

void PlaybackServiceImpl::pause() {
    if (m_state != State::Playing) {
        return;
    }
    // 暫停就關裝置（獨占模式時讓出裝置），繼續時從這個位置重開
    poll();
    if (m_state != State::Playing) {
        return;
    }
    stopEngine();
    setState(State::Paused);
}

void PlaybackServiceImpl::togglePause() {
    if (m_state == State::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackServiceImpl::stop() {
    stopEngine();
    setState(State::Stopped);
    setPosition(0);
    updateOutputStatus();
}

void PlaybackServiceImpl::next() {
    const auto current = m_queue.current();
    const auto target = current ? m_queue.nextAfter(current->id, false) : m_queue.entryAt(0);
    if (target) {
        startEntry(target);
    }
}

void PlaybackServiceImpl::previous() {
    const auto current = m_queue.current();
    if (!current) {
        return;
    }
    const auto before = m_queue.previousBefore(current->id);
    if (m_positionMs > kRestartThresholdMs || !before) {
        seek(0);
        return;
    }
    startEntry(before);
}

void PlaybackServiceImpl::seek(qint64 positionMs) {
    positionMs = std::max<qint64>(0, positionMs);
    if (m_durationMs >= 0) {
        positionMs = std::min(positionMs, m_durationMs);
    }
    if (m_state == State::Paused) {
        setPosition(positionMs);
        return;
    }
    if (m_state != State::Playing) {
        return;
    }
    // 不重開裝置的跳轉；下一首已經寫進緩衝（最後約 0.5 秒）時引擎做不到，改成從該位置重開
    if (m_sourceRate > 0 && m_engine.seek(positionMs * m_sourceRate / 1000)) {
        setPosition(positionMs);
        return;
    }
    startEntry(m_queue.current(), positionMs);
}

} // namespace auralis::core
