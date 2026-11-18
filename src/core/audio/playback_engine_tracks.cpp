// 播放引擎的曲目接續（US-05）：預開下一首、同格式無縫接續、換格式時的靜音切換、區段與位置。
// 除了 setNextTrackProvider() 與 position()，這裡的函式都在解碼執行緒（或 play() 先填緩衝時）呼叫。
#include "audio/playback_engine.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>

namespace auralis::core {

namespace {

/// 換格式時頭尾各送多少毫秒的數位靜音：關裝置前確定裝置已經在送靜音、重開後 DAC 鎖定新取樣率時
/// 也是靜音，切換處不會有爆音（AC-05-3）。
constexpr int kSwitchSilenceMs = 50;

} // namespace

void PlaybackEngine::setNextTrackProvider(NextTrackProvider* provider) noexcept {
    m_provider.store(provider, std::memory_order_release);
}

PlaybackEngine::Position PlaybackEngine::position() const {
    const std::lock_guard lock(m_stateMutex);
    const qint64 rendered = m_renderedFrames.load(std::memory_order_relaxed);
    // 送進裝置的資料已經越過哪一首的開始位置，正在聽的就是那一首
    Position position;
    if (m_seekInFlight) {
        position.token = m_seekToken.load(std::memory_order_relaxed);
        position.frames = toOutputFrames(m_seekFrame.load(std::memory_order_relaxed));
        return position;
    }
    if (m_marks.empty()) {
        return position;
    }
    const TrackMark* current = &m_marks.front();
    for (const TrackMark& mark : m_marks) {
        if (mark.start > rendered) {
            break;
        }
        current = &mark;
    }
    position.token = current->token;
    position.frames = std::max<qint64>(0, rendered - current->start);
    return position;
}

void PlaybackEngine::prefetchNext() {
    if (m_askedNext) {
        return;
    }
    m_askedNext = true;
    NextTrackProvider* provider = m_provider.load(std::memory_order_acquire);
    if (!provider) {
        return;
    }
    // 最後一個標記就是正在解碼的這首（撤回後再問時也是同一首）
    m_next = provider->nextTrackAfter(m_marks.empty() ? 0 : m_marks.back().token);
    if (m_next && !m_next->source) {
        m_next.reset();
    }
}

bool PlaybackEngine::advanceSource() {
    prefetchNext();
    if (!m_next) {
        return false;
    }
    const AudioFormat format = m_next->source->format();
    if (!(format == m_status.sourceFormat)) {
        // 格式不同：等緩衝裡的這首送完再重開裝置（decodeLoop → switchOutput）
        m_reopenPending = true;
        return true;
    }
    // 格式相同：在同一個裝置上緊接著寫進緩衝。處理器每首一個新實例（註冊的變更從下一首起作用）
    if (!m_activeDsdSink) {
        auto processors = m_chain.createAll();
        for (const auto& processor : processors) {
            processor->prepareTrack(format, m_next->track);
        }
        const std::lock_guard lock(m_stateMutex);
        m_processors.swap(processors);
    }
    EngineTrack next = std::move(*m_next);
    m_next.reset();
    auto begun = beginTrack(std::move(next));
    if (!begun) {
        m_error = begun.error();
        return false;
    }
    return true;
}

Result<bool> PlaybackEngine::beginTrack(EngineTrack track) {
    const qint64 offset = std::max<qint64>(0, track.offsetFrame);
    {
        const std::lock_guard lock(m_stateMutex);
        // 已經聽完的曲目不用再記
        const qint64 rendered = m_renderedFrames.load(std::memory_order_relaxed);
        while (m_marks.size() > 1 && m_marks[1].start <= rendered) {
            m_marks.pop_front();
        }
        // 從中間開始時，位置一開始就是 offset（從區段起點算）
        m_marks.push_back(TrackMark{track.token, m_writtenFrames - toOutputFrames(offset)});
    }
    const qint64 first = track.startFrame + offset;
    if (first > 0) {
        auto sought = track.source->seek(first);
        if (!sought) {
            return sought.error();
        }
    }
    m_source = std::move(track.source);
    m_sourceFrame = first;
    m_trackWriteStart = m_writtenFrames;
    m_trackStart = track.startFrame;
    m_trackEnd = track.endFrame;
    m_segmentLeft = track.endFrame >= 0 ? std::max<qint64>(0, track.endFrame - first) : -1;
    const qint64 length = m_source->lengthFrames();
    m_expectedLeft = m_segmentLeft >= 0 ? m_segmentLeft
                     : length >= 0      ? std::max<qint64>(0, length - first)
                                        : -1;
    m_askedNext = false;
    return true;
}

qint64 PlaybackEngine::toOutputFrames(qint64 sourceFrames) const noexcept {
    const qint64 in = framesPerSecond(m_status.sourceFormat);
    return in > 0 ? sourceFrames * framesPerSecond(m_status.outputFormat) / in : sourceFrames;
}

bool PlaybackEngine::seek(qint64 frame) {
    if (!m_backend || frame < 0 || m_decodeDone.load(std::memory_order_acquire)) {
        return false;
    }
    {
        const std::lock_guard lock(m_stateMutex);
        // 下一首已經寫進緩衝（正在聽的不是正在解碼的）：舊音源已經關了，只能由上層重來
        if (m_marks.empty() ||
            m_marks.back().start > m_renderedFrames.load(std::memory_order_relaxed) ||
            m_decodeDone.load(std::memory_order_acquire)) {
            return false;
        }
        m_seekToken.store(m_marks.back().token, std::memory_order_relaxed);
        // 和 position() 同一把鎖：要求一送出，位置就是目標
        m_seekFrame.store(frame, std::memory_order_relaxed);
        m_seekPending.store(true, std::memory_order_release);
        m_seekInFlight = true;
    }
    return true;
}

void PlaybackEngine::invalidateNextTrack() noexcept {
    m_dropNext.store(true, std::memory_order_release);
}

bool PlaybackEngine::handleSeek() {
    {
        const std::lock_guard lock(m_stateMutex);
        // 要求之後已經換到下一首：放棄這次跳轉
        if (m_marks.empty() ||
            m_marks.back().token != m_seekToken.load(std::memory_order_relaxed)) {
            m_seekInFlight = false;
            return true;
        }
    }
    // 1. 請音訊執行緒丟掉緩衝裡舊位置的資料；這段時間補的靜音不算欠載
    m_switching.store(true, std::memory_order_release);
    m_flushRequest.store(true, std::memory_order_release);
    while (m_flushRequest.load(std::memory_order_acquire)) {
        if (m_stopDecoding.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(kFullWait);
    }
    // 2. 音源移到新位置；已經預開的下一首仍然是這首之後的那首，保留
    const qint64 frame = m_seekFrame.load(std::memory_order_relaxed);
    const qint64 target = m_trackStart + frame;
    auto sought = m_source->seek(target);
    if (!sought) {
        m_error = sought.error();
        m_switching.store(false, std::memory_order_release);
        const std::lock_guard lock(m_stateMutex);
        m_seekInFlight = false;
        return false;
    }
    m_reopenPending = false;
    m_sourceFrame = target;
    m_segmentLeft = m_trackEnd >= 0 ? std::max<qint64>(0, m_trackEnd - target) : -1;
    const qint64 length = m_source->lengthFrames();
    m_expectedLeft = m_segmentLeft >= 0 ? m_segmentLeft
                     : length >= 0      ? std::max<qint64>(0, length - target)
                                        : -1;
    {
        // 緩衝是空的，送出的框數不會再變：從這裡重新對齊寫入位置與標記
        const std::lock_guard lock(m_stateMutex);
        const qint64 rendered = m_renderedFrames.load(std::memory_order_relaxed);
        m_marks.assign(1, TrackMark{m_seekToken.load(std::memory_order_relaxed),
                                    rendered - toOutputFrames(frame)});
        m_writtenFrames = rendered;
        m_trackWriteStart = rendered;
        m_gainSettledAt = rendered;
        // 這段時間又有新的要求（讀 m_seekFrame 之後才送來的）時，位置繼續回報它的目標
        m_seekInFlight = m_seekPending.load(std::memory_order_acquire);
    }
    // 3. 先填滿緩衝再恢復欠載計數
    bool more = true;
    while (more && !m_reopenPending && hasRoomForChunk()) {
        more = produceChunk();
    }
    m_switching.store(false, std::memory_order_release);
    return more;
}

void PlaybackEngine::dropNext() {
    // 換格式等著重開裝置時也撤得回：目前的音源已經讀到結尾，下一個區塊會再問一次提供者
    m_next.reset();
    m_reopenPending = false;
    m_askedNext = false;
}

bool PlaybackEngine::switchOutput() {
    const auto stopRequested = [this] {
        return m_stopDecoding.load(std::memory_order_acquire);
    };
    // 1. 排空：讓裝置把這首剩下的資料送完，之後 render 補的都是靜音（刻意排空，不算欠載）。
    // DSD 先經包裝器寫一段靜音碼：有濾波狀態的包裝器（轉 PCM）要再餵濾波器長度的資料，
    // 曲目最後一段才推得出來，不會被 render 補的靜音取代（50 ms 遠大於濾波器長度）
    m_switching.store(true, std::memory_order_release);
    if (m_activeDsdSink && !writeSilence(kSwitchSilenceMs)) {
        return true;
    }
    while (m_ring->readable() > 0) {
        if (stopRequested()) {
            return true;
        }
        std::this_thread::sleep_for(kFullWait);
    }
    // 2. 再送一段靜音，確定裝置緩衝裡已經全是靜音才停，不會在有聲音的地方被切斷
    const qint64 silence = framesPerSecond(m_status.outputFormat) * kSwitchSilenceMs / 1000;
    const qint64 base = m_paddedFrames.load(std::memory_order_acquire);
    while (m_paddedFrames.load(std::memory_order_acquire) - base < silence) {
        if (stopRequested()) {
            return true;
        }
        std::this_thread::sleep_for(kFullWait);
    }
    m_backend->stop();
    m_backend->close();

    // 3. 以下一首的格式重開裝置；開不了時回報是哪一首（種類看後端的 lastErrorKind()）
    m_reopenPending = false;
    EngineTrack next = std::move(*m_next);
    m_next.reset();
    const auto fail = [this, token = next.token](Error error) {
        const std::lock_guard lock(m_stateMutex);
        m_marks.assign(1, TrackMark{token, 0});
        m_renderedFrames.store(0, std::memory_order_relaxed);
        // 後端沒在跑，換成空的緩衝讓 isFinished() 成立
        m_ring = std::make_unique<RingBuffer>(1);
        m_error = std::move(error);
        return false;
    };
    auto opened = openOutput(next.source->format(), next.track, *m_backend);
    if (!opened) {
        return fail(opened.error());
    }
    {
        const std::lock_guard lock(m_stateMutex);
        m_marks.clear();
        m_renderedFrames.store(0, std::memory_order_relaxed);
    }
    m_writtenFrames = 0;
    m_gainSettledAt = 0;

    // 4. 新裝置先送一段靜音，再接下一首；先填滿緩衝才啟動
    writeSilence(kSwitchSilenceMs);
    auto begun = beginTrack(std::move(next));
    if (!begun) {
        return fail(begun.error());
    }
    bool more = true;
    while (more && !m_reopenPending && hasRoomForChunk()) {
        more = produceChunk();
    }
    m_switching.store(false, std::memory_order_release);
    auto started = m_backend->start(*this);
    if (!started) {
        return fail(started.error());
    }
    return more;
}

bool PlaybackEngine::writeSilence(int milliseconds) {
    // PCM 的數位靜音是 0；DSD 的靜音位元組是 0x69，交給包裝器轉成它的靜音封包（例：DoP）
    const qint64 total = framesPerSecond(m_status.sourceFormat) * milliseconds / 1000;
    std::memset(m_readBuffer.data(), m_activeDsdSink ? 0x69 : 0, m_readBuffer.size());
    for (qint64 done = 0; done < total;) {
        // 緩衝還有這首的資料時（換格式排空前）等裝置送出空間
        while (!hasRoomForChunk()) {
            if (m_stopDecoding.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::sleep_for(kFullWait);
        }
        const qint64 frames = std::min(kChunkFrames, total - done);
        if (m_activeDsdSink) {
            const qint64 out =
                m_activeDsdSink->convert(m_readBuffer.data(), frames, m_dsdBuffer.data());
            writeToRing(m_dsdBuffer.data(), out);
        } else {
            writeToRing(m_readBuffer.data(), frames);
        }
        done += frames;
    }
    return true;
}

void PlaybackEngine::prepareSilence() {
    // 解碼執行緒、音訊執行緒沒在跑：可以配置。DSD 轉一個區塊的靜音碼（整數個 DSD 樣本框，
    // 不記狀態的包裝器轉出的封包會依序重複，例：DoP 的標記 0x05、0xFA）
    m_silence.clear();
    m_silenceFrames = 0;
    m_ringFramesOut = 0;
    m_silencePhase = 0;
    if (!m_activeDsdSink) {
        return;
    }
    std::memset(m_readBuffer.data(), 0x69, m_readBuffer.size());
    const qint64 frames =
        m_activeDsdSink->convert(m_readBuffer.data(), kChunkFrames, m_dsdBuffer.data());
    m_silence.assign(m_dsdBuffer.begin(), m_dsdBuffer.begin() + frames * m_outputFrameBytes);
    m_silenceFrames = frames;
}

void PlaybackEngine::padSilence(std::byte* dest, std::size_t bytes) noexcept {
    // 即時路徑：只複製開裝置時準備好的靜音，不配置
    if (m_silenceFrames == 0) {
        std::memset(dest, 0, bytes);
        return;
    }
    const auto frameBytes = static_cast<std::size_t>(m_outputFrameBytes);
    while (bytes > 0) {
        const std::size_t room =
            static_cast<std::size_t>(m_silenceFrames - m_silencePhase) * frameBytes;
        const std::size_t n = std::min(bytes, room);
        std::memcpy(dest, m_silence.data() + m_silencePhase * m_outputFrameBytes, n);
        dest += n;
        bytes -= n;
        m_silencePhase = (m_silencePhase + static_cast<qint64>(n / frameBytes)) % m_silenceFrames;
    }
}

Result<bool> PlaybackEngine::checkDsdPath(const IOutputBackend& backend) {
    // 不改變位元流的包裝器（DoP、原生 DSD）要位元完美的路徑：經混音器（共享模式）DoP 封包
    // 會變成噪音，所以不開裝置、不送出，由上層跳過這首（模組還沒把包裝器換成轉 PCM 時）
    const QString change = backend.signalChange();
    if (change.isEmpty() || !m_dsdSink || !m_dsdSink->reason().isEmpty()) {
        return true;
    }
    m_dsdPathRejected = true;
    return Error{QCoreApplication::translate("PlaybackEngine",
                                             "%1會改動 DSD 位元流，請把 DSD 輸出改成「轉 PCM」")
                     .arg(change),
                 0, QStringLiteral("PlaybackEngine::play")};
}

} // namespace auralis::core
