// PlaybackService：開檔與 token 登記、下一首提供者（引擎在解碼執行緒呼叫）。
#include "playback/playback_service_impl.h"

#include "library/cue_tracks.h"

namespace auralis::core {

Result<EngineTrack> PlaybackServiceImpl::openTrack(const QueueEntry& entry, qint64 offsetMs) {
    // 整檔或 CUE 區段（身分是 .cue 路徑加軌號）都由核心的 openTrack 開好
    auto opened = core::openTrack(entry.track, m_sources);
    if (!opened) {
        return opened.error();
    }
    EngineTrack track = std::move(opened).value();
    TokenInfo info;
    info.entryId = entry.id;
    info.sourceRate = framesPerSecond(track.source->format());
    // 長度是這一段的長度：到檔尾的區段用音源長度扣掉起點
    const qint64 sourceLength = track.source->lengthFrames();
    const qint64 length = track.endFrame >= 0 ? track.endFrame - track.startFrame
                          : sourceLength >= 0 ? sourceLength - track.startFrame
                                              : -1;
    if (info.sourceRate > 0) {
        info.durationMs = length >= 0 ? length * 1000 / info.sourceRate : -1;
        track.offsetFrame = offsetMs * info.sourceRate / 1000;
    }
    const std::lock_guard lock(m_tokenMutex);
    track.token = m_nextToken++;
    m_tokens.emplace(track.token, info);
    return track;
}

std::optional<PlaybackServiceImpl::TokenInfo> PlaybackServiceImpl::tokenInfo(quint64 token) const {
    const std::lock_guard lock(m_tokenMutex);
    const auto it = m_tokens.find(token);
    if (it == m_tokens.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ---- 下一首提供者（解碼執行緒） ----

std::optional<EngineTrack> PlaybackServiceImpl::nextTrackAfter(quint64 token) {
    const auto info = tokenInfo(token);
    if (!info) {
        return std::nullopt;
    }
    quint64 after = info->entryId;
    // 開不了的一首一首跳過，最多試整個佇列一輪
    for (qsizetype tries = m_queue.entries().size(); tries >= 0; --tries) {
        const auto next = m_queue.nextAfter(after, true);
        if (!next) {
            return std::nullopt;
        }
        auto opened = openTrack(*next, 0);
        if (opened) {
            return std::move(opened).value();
        }
        m_queue.setUnplayable(next->id, true);
        // 訊號一律在 UI 執行緒發
        QMetaObject::invokeMethod(
            this,
            [this, track = next->track, reason = opened.error().message] {
                emit trackSkipped(track, reason);
                emit queueChanged();
            },
            Qt::QueuedConnection);
        after = next->id;
    }
    return std::nullopt;
}

} // namespace auralis::core
