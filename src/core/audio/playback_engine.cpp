#include "audio/playback_engine.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>

namespace auralis::core {

namespace {

/// 環形緩衝的長度（秒）：要蓋過介面卡頓、掃描等造成的解碼延遲。
constexpr int kBufferMilliseconds = 500;
/// 目前曲目剩幾秒時預開下一首（開檔與解碼第一個區塊的時間要在緩衝播完前做完）。
constexpr qint64 kPrefetchSeconds = 5;

/// 處理器以外、輸出路徑本身改變訊號的原因（AC-02-3）：後端（共享模式）、DSD 包裝器（轉 PCM）、
/// 裝置不收浮點時浮點來源被轉成整數（有損格式在獨占模式）。
QStringList pathReasons(const PlaybackEngine::Status& status, const IOutputBackend& backend,
                        const DsdSink* dsdSink) {
    QStringList reasons;
    if (const QString change = backend.signalChange(); !change.isEmpty()) {
        reasons << change;
    }
    if (dsdSink != nullptr) {
        if (const QString reason = dsdSink->reason(); !reason.isEmpty()) {
            reasons << reason;
        }
    } else if (status.outputFormat.isFloat && !status.deviceFormat.isFloat) {
        reasons << QCoreApplication::translate("PlaybackEngine", "浮點轉成 %1 bit 整數")
                       .arg(status.deviceFormat.bits);
    }
    return reasons;
}

} // namespace

qint64 PlaybackEngine::framesPerSecond(const AudioFormat& format) noexcept {
    return format.rate > 0 ? format.rate : format.dsdRate / 32;
}

PlaybackEngine::PlaybackEngine(ProcessorChainImpl& processors) : m_chain(processors) {}

PlaybackEngine::~PlaybackEngine() {
    stop();
}

void PlaybackEngine::setDsdSink(std::shared_ptr<DsdSink> sink) {
    m_dsdSink = std::move(sink);
}

Result<PlaybackEngine::Status> PlaybackEngine::play(std::unique_ptr<IAudioSource> source,
                                                    IOutputBackend& backend) {
    EngineTrack track;
    track.source = std::move(source);
    return play(std::move(track), backend);
}

Result<PlaybackEngine::Status> PlaybackEngine::play(EngineTrack track, IOutputBackend& backend,
                                                    const std::function<bool()>& cancelled) {
    stop();
    if (!track.source) {
        return Error{QStringLiteral("沒有音源"), 0, QStringLiteral("PlaybackEngine::play")};
    }
    auto opened = openOutput(track.source->format(), track.track, backend);
    if (!opened) {
        return opened.error();
    }
    m_error.reset();
    m_renderedFrames.store(0, std::memory_order_relaxed);
    m_underruns.store(0, std::memory_order_relaxed);
    m_stopDecoding.store(false, std::memory_order_relaxed);
    m_decodeDone.store(false, std::memory_order_relaxed);
    m_switching.store(false, std::memory_order_relaxed);
    m_flushRequest.store(false, std::memory_order_relaxed);
    m_seekPending.store(false, std::memory_order_relaxed);
    m_dropNext.store(false, std::memory_order_relaxed);
    m_volumeDirty.store(false, std::memory_order_relaxed);
    m_unwriteRequest.store(false, std::memory_order_relaxed);
    m_writtenFrames = 0;
    m_gainSettledAt = 0;
    // 前面沒有聲音，音量直接從設定值開始，不用過渡
    m_volume.snap();
    m_next.reset();
    m_reopenPending = false;
    {
        const std::lock_guard lock(m_stateMutex);
        m_marks.clear();
        m_seekInFlight = false;
    }
    m_backend = &backend;
    auto begun = beginTrack(std::move(track));
    if (!begun) {
        const Error error = begun.error();
        stop();
        return error;
    }

    // 先填滿緩衝，裝置一開始就有資料，不會以欠載開場（格式不同的下一首等裝置跑起來再換）
    const bool more = prefill(cancelled);
    if (cancelled && cancelled()) {
        stop();
        return Error{QStringLiteral("開始播放已取消"), 0, QStringLiteral("PlaybackEngine::play")};
    }
    auto started = backend.start(*this);
    if (!started) {
        const Error error = started.error();
        stop();
        return error;
    }
    // 解碼執行緒在後端啟動之後才開：換格式時由它停止與重開後端，不和這裡同時碰後端
    if (more) {
        m_decodeThread = std::thread([this] { decodeLoop(); });
    }
    return this->status();
}

Result<PlaybackEngine::Status> PlaybackEngine::openOutput(const AudioFormat& sourceFormat,
                                                          const TrackRef& track,
                                                          IOutputBackend& backend) {
    m_dsdPathRejected = false;
    Status status;
    status.sourceFormat = sourceFormat;
    status.outputFormat = sourceFormat;
    status.backend = backend.name();

    qint64 outputChunkFrames = kChunkFrames;
    std::vector<std::unique_ptr<IProcessor>> processors;
    std::shared_ptr<DsdSink> dsdSink;
    const DsdFormat dsd = DsdFormat::from(sourceFormat);
    if (dsd.isValid()) {
        // DSD 不經過處理器鏈：任何改動都會把位元流變成噪音（音量只接在轉 PCM 的輸出之後）
        if (!m_dsdSink) {
            return Error{QStringLiteral("沒有 DSD 包裝器（進階輸出模組未啟用）"), 0,
                         QStringLiteral("PlaybackEngine::play")};
        }
        if (auto checked = checkDsdPath(backend); !checked) {
            return checked.error();
        }
        auto opened = m_dsdSink->open(dsd);
        if (!opened) {
            return opened.error();
        }
        status.outputFormat = opened.value();
        outputChunkFrames = m_dsdSink->maxOutputFrames(kChunkFrames);
        dsdSink = m_dsdSink;
        status.volumeAvailable = prepareDsdVolume(status.outputFormat, outputChunkFrames);
    } else {
        processors = m_chain.createAll();
        for (const auto& processor : processors) {
            processor->prepareTrack(sourceFormat, track);
        }
        m_volume.prepare(sourceFormat);
    }

    auto device = backend.open(status.outputFormat);
    if (!device) {
        return device.error();
    }
    status.deviceFormat = device.value();
    status.pathReasons = pathReasons(status, backend, dsdSink.get());
    m_activeDsdSink = std::move(dsdSink);

    // 播放中不再配置：緩衝都在這裡一次配好
    const int outputFrameBytes = status.outputFormat.bytesPerFrame();
    const qint64 bufferFrames = framesPerSecond(status.outputFormat) * kBufferMilliseconds / 1000;
    const qint64 minFrames = outputChunkFrames * 4;
    m_ring = std::make_unique<RingBuffer>(static_cast<std::size_t>(
        (bufferFrames > minFrames ? bufferFrames : minFrames) * outputFrameBytes));
    m_readBuffer.assign(static_cast<std::size_t>(kChunkFrames * sourceFormat.bytesPerFrame()),
                        std::byte{0});
    if (m_activeDsdSink) {
        m_dsdBuffer.assign(static_cast<std::size_t>(outputChunkFrames * outputFrameBytes),
                           std::byte{0});
    } else {
        m_floatBuffer.assign(static_cast<std::size_t>(kChunkFrames * sourceFormat.channels), 0.0);
    }
    m_outputFrameBytes = outputFrameBytes;
    prepareSilence();
    {
        const std::lock_guard lock(m_stateMutex);
        m_processors.swap(processors);
        m_status = status;
    }
    return status;
}

void PlaybackEngine::stop() {
    if (!m_backend) {
        return;
    }
    // 先停解碼執行緒：換格式時它會停止與重開後端，所以等它結束才由這裡停後端；
    // 後端的 stop() 回傳後音訊執行緒不會再呼叫 render()，之後才能動緩衝
    m_stopDecoding.store(true, std::memory_order_release);
    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
    m_backend->stop();
    m_backend->close();
    m_backend = nullptr;
    m_source.reset();
    m_next.reset();
    {
        const std::lock_guard lock(m_stateMutex);
        m_processors.clear();
        m_seekInFlight = false;
    }
    m_activeDsdSink.reset();
    m_ring.reset();
    m_outputFrameBytes = 0;
}

bool PlaybackEngine::isFinished() const noexcept {
    return m_ring && m_decodeDone.load(std::memory_order_acquire) && m_ring->readable() == 0;
}

std::optional<Error> PlaybackEngine::decodeError() const {
    if (!m_decodeDone.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return m_error;
}

PlaybackEngine::Status PlaybackEngine::status() const {
    const std::lock_guard lock(m_stateMutex);
    Status status = m_status;
    status.reasons = status.pathReasons;
    status.bitPerfect = status.reasons.isEmpty();
    for (const auto& processor : m_processors) {
        if (processor->isActive()) {
            status.bitPerfect = false;
            status.reasons.append(processor->reason());
        }
    }
    // 音量照使用者的設定回報（改回 100% 時增益還要約 30 毫秒才回到 1.0，那一小段不另外顯示）
    if (status.volumeAvailable && m_volume.percent() != 100) {
        status.bitPerfect = false;
        status.volumeReason = m_volume.reason();
        status.reasons.append(status.volumeReason);
    }
    return status;
}

qint64 PlaybackEngine::render(std::byte* dest, qint64 frames) noexcept {
    // 即時路徑：不配置、不加鎖、不 I/O；只碰環形緩衝與原子變數
    if (m_flushRequest.load(std::memory_order_acquire)) {
        // 跳轉：丟掉舊位置的資料（讀取位置只有消費者能動）；解碼執行緒等這裡做完才寫新位置
        m_ringFramesOut +=
            static_cast<qint64>(m_ring->discard(m_ring->readable())) / m_outputFrameBytes;
        m_flushRequest.store(false, std::memory_order_release);
    }
    if (m_unwriteRequest.load(std::memory_order_acquire)) {
        serveUnwrite();
    }
    const auto bytes = static_cast<std::size_t>(frames * m_outputFrameBytes);
    const std::size_t got = m_ring->read(dest, bytes);
    if (got > 0 && m_silenceFrames > 0) {
        m_ringFramesOut += static_cast<qint64>(got) / m_outputFrameBytes;
        m_silencePhase = m_ringFramesOut % m_silenceFrames;
    }
    if (got < bytes) {
        // 補靜音（排空、欠載、播完）：PCM 是 0，DSD 是包裝器轉換過的靜音碼（AC-04-6）
        padSilence(dest + got, bytes - got);
        m_paddedFrames.fetch_add(static_cast<qint64>(bytes - got) / m_outputFrameBytes,
                                 std::memory_order_relaxed);
        if (!m_decodeDone.load(std::memory_order_acquire) &&
            !m_switching.load(std::memory_order_acquire)) {
            m_underruns.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const qint64 rendered = static_cast<qint64>(got) / m_outputFrameBytes;
    m_renderedFrames.fetch_add(rendered, std::memory_order_relaxed);
    return rendered;
}

bool PlaybackEngine::hasRoomForChunk() const noexcept {
    const qint64 chunkFrames =
        m_activeDsdSink ? m_activeDsdSink->maxOutputFrames(kChunkFrames) : kChunkFrames;
    return m_ring->writable() >= static_cast<std::size_t>(chunkFrames * m_outputFrameBytes);
}

bool PlaybackEngine::produceChunk() {
    // 區段（CUE 曲目）只讀到終點
    const qint64 want = m_segmentLeft >= 0 ? std::min(kChunkFrames, m_segmentLeft) : kChunkFrames;
    qint64 frames = 0;
    if (want > 0) {
        auto read = m_source->read(m_readBuffer.data(), want);
        if (!read) {
            m_error = read.error();
            return false;
        }
        frames = read.value();
    }
    if (frames == 0) {
        return advanceSource();
    }
    const AudioFormat& format = m_status.sourceFormat;
    m_sourceFrame += frames;
    if (m_segmentLeft >= 0) {
        m_segmentLeft -= frames;
    }
    if (m_expectedLeft >= 0) {
        m_expectedLeft = std::max<qint64>(0, m_expectedLeft - frames);
        if (m_expectedLeft <= framesPerSecond(format) * kPrefetchSeconds) {
            prefetchNext();
        }
    }
    if (m_activeDsdSink) {
        produceDsdChunk(frames);
        return true;
    }
    bool anyActive = false;
    for (const auto& processor : m_processors) {
        anyActive = anyActive || processor->isActive();
    }
    // 音量 100% 而且沒有在過渡時不作用：和沒有音量控制時走同一條整數直通
    const bool volumeActive = m_volume.isActive();
    qint64 rampFrames = 0;
    if (m_volume.isMuted()) {
        // 0%：數位靜音（全 0，不加抖動）
        std::memset(m_readBuffer.data(), 0,
                    static_cast<std::size_t>(frames * format.bytesPerFrame()));
    } else if (anyActive || volumeActive) {
        // 有處理器作用：64 位元浮點處理後以 TPDF 抖動轉回原本的位元深度
        toDouble(m_readBuffer.data(), format, frames, m_floatBuffer.data());
        for (const auto& processor : m_processors) {
            if (processor->isActive()) {
                processor->process(m_floatBuffer.data(), frames, format.channels);
            }
        }
        if (volumeActive) {
            m_volume.process(m_floatBuffer.data(), frames, format.channels);
            rampFrames = m_volume.lastRampFrames();
        }
        fromDouble(m_floatBuffer.data(), format, frames, m_readBuffer.data(), m_dither);
    }
    if (rampFrames > 0) {
        m_gainSettledAt = m_writtenFrames + rampFrames;
    }
    writeToRing(m_readBuffer.data(), frames);
    return true;
}

void PlaybackEngine::writeToRing(const std::byte* data, qint64 frames) {
    m_ring->write(data, static_cast<std::size_t>(frames * m_outputFrameBytes));
    m_writtenFrames += frames;
}

void PlaybackEngine::decodeLoop() {
    while (!m_stopDecoding.load(std::memory_order_acquire)) {
        if (m_seekPending.exchange(false, std::memory_order_acq_rel)) {
            if (!handleSeek()) {
                finishDecoding(m_error);
                return;
            }
            continue;
        }
        if (m_dropNext.exchange(false, std::memory_order_acq_rel)) {
            dropNext();
        }
        if (m_volumeDirty.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now() - m_lastVolumeRedo >= kVolumeRedoInterval) {
            m_volumeDirty.store(false, std::memory_order_release);
            if (!redoForVolume()) {
                finishDecoding(m_error);
                return;
            }
            m_lastVolumeRedo = std::chrono::steady_clock::now();
            continue;
        }
        if (m_reopenPending) {
            if (!switchOutput()) {
                finishDecoding(m_error);
                return;
            }
            continue;
        }
        if (!hasRoomForChunk()) {
            std::this_thread::sleep_for(kFullWait);
            continue;
        }
        if (!produceChunk()) {
            finishDecoding(m_error);
            return;
        }
    }
}

void PlaybackEngine::finishDecoding(std::optional<Error> error) {
    m_error = std::move(error);
    // 解完時還沒處理的跳轉不會再處理：位置照實際送出的回報（seek() 在同一把鎖內看 m_decodeDone）
    const std::lock_guard lock(m_stateMutex);
    m_seekInFlight = false;
    m_decodeDone.store(true, std::memory_order_release);
}

} // namespace auralis::core
