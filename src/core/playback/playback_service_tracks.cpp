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

// ---- 開始播放的背景執行緒 ----

void PlaybackServiceImpl::postJob(StartJob job) {
    {
        const std::lock_guard lock(m_startMutex);
        job.seq = ++m_startSeq;
        m_job = std::move(job);
    }
    m_startWake.notify_one();
    m_starting = true;
}

void PlaybackServiceImpl::starterLoop() {
    std::unique_lock lock(m_startMutex);
    for (;;) {
        m_startWake.wait(lock, [this] { return m_quit || m_job.has_value(); });
        if (m_quit) {
            return;
        }
        StartJob job = std::move(*m_job);
        m_job.reset();
        m_jobRunning = true;
        lock.unlock();
        StartResult result = runJob(std::move(job));
        lock.lock();
        m_jobRunning = false;
        m_results.push_back(std::move(result));
        m_startIdle.notify_all();
        // 結果在 UI 執行緒處理；服務先解構時 Qt 丟掉這個呼叫
        QMetaObject::invokeMethod(this, [this] { drainStartResults(); }, Qt::QueuedConnection);
    }
}

PlaybackServiceImpl::StartResult PlaybackServiceImpl::runJob(StartJob job) {
    StartResult result;
    result.seq = job.seq;
    result.entry = job.entry;
    result.offsetMs = job.offsetMs;
    // 先停：之後換 DSD 包裝器、開新音源時不會和解碼執行緒同時碰引擎
    m_engine.stop();
    const auto cancelled = [this, seq = job.seq] {
        return m_startSeq.load() != seq;
    };
    if (!job.entry || cancelled()) {
        return result;
    }
    auto opened = openTrack(*job.entry, job.offsetMs);
    if (!opened) {
        result.openError = opened.error();
        return result;
    }
    if (cancelled()) {
        return result;
    }
    result.token = opened.value().token;
    m_engine.setDsdSink(std::move(job.dsdSink));
    m_engine.setNextTrackProvider(this);
    auto started = m_engine.play(std::move(opened).value(), *job.backend, cancelled);
    if (!started) {
        result.startError = started.error();
        result.errorKind = job.backend->lastErrorKind();
        result.dsdRejected = m_engine.dsdPathRejected();
        return result;
    }
    result.outputFormat = started.value().outputFormat;
    return result;
}

void PlaybackServiceImpl::drainStartResults() {
    std::deque<StartResult> results;
    {
        const std::lock_guard lock(m_startMutex);
        results.swap(m_results);
    }
    for (const StartResult& result : results) {
        finishStart(result);
    }
}

void PlaybackServiceImpl::waitForStart() {
    while (m_starting) {
        {
            std::unique_lock lock(m_startMutex);
            m_startIdle.wait(lock, [this] { return !m_job && !m_jobRunning; });
        }
        // 處理結果時可能又送出下一首（跳過開不了的），所以再等一輪
        drainStartResults();
    }
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
