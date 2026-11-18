// 播放引擎的音量（AC-02-7）：播放中改音量時，把緩衝裡還沒送出的資料收回來用新音量重做，
// 不重開裝置、不等整個緩衝（約 0.5 秒）播完。serveUnwrite() 在即時音訊執行緒，其他在解碼執行緒
// （setVolume() 是呼叫端的執行緒）。DSD 轉 PCM 時音量接在包裝器之後（T-87）。
#include "audio/playback_engine.h"

#include <algorithm>
#include <cstring>

namespace auralis::core {

bool PlaybackEngine::prepareDsdVolume(const AudioFormat& outputFormat, qint64 outputChunkFrames) {
    // DoP 與原生 DSD 送的是位元流本身，任何增益都會把它變成噪音；轉 PCM 的輸出是一般 PCM，
    // 音量接在包裝器之後（使用者決定；轉 PCM 本身的增益維持 0 dB）
    if (!m_dsdSink->convertsToPcm() || outputFormat.dsdRate != 0 || outputFormat.rate == 0) {
        return false;
    }
    m_volume.prepare(outputFormat);
    m_floatBuffer.assign(static_cast<std::size_t>(outputChunkFrames * outputFormat.channels), 0.0);
    return true;
}

void PlaybackEngine::produceDsdChunk(qint64 frames) {
    const qint64 out = m_activeDsdSink->convert(m_readBuffer.data(), frames, m_dsdBuffer.data());
    const AudioFormat& format = m_status.outputFormat;
    if (m_status.volumeAvailable && m_volume.isMuted()) {
        // 0%：數位靜音（全 0，不加抖動）
        std::memset(m_dsdBuffer.data(), 0, static_cast<std::size_t>(out * format.bytesPerFrame()));
    } else if (m_status.volumeAvailable && m_volume.isActive()) {
        // 和 PCM 曲目同一套：64 位元浮點處理後以 TPDF 抖動轉回包裝器輸出的位元深度。
        // 不重做緩衝（音源與包裝器的濾波狀態移不回去），所以不記增益穩定的位置
        toDouble(m_dsdBuffer.data(), format, out, m_floatBuffer.data());
        m_volume.process(m_floatBuffer.data(), out, format.channels);
        fromDouble(m_floatBuffer.data(), format, out, m_dsdBuffer.data(), m_dither);
    }
    writeToRing(m_dsdBuffer.data(), out);
}

void PlaybackEngine::setVolume(int percent) noexcept {
    m_volume.setPercent(percent);
    m_volumeDirty.store(true, std::memory_order_release);
}

bool PlaybackEngine::redoForVolume() {
    // 從這裡起寫進緩衝的區塊都往新音量過渡；斜坡的起點就是下面收回之後的接續處
    m_volume.latch();
    // 只收回目前解碼的這首（上一首的音源已經關了）、而且是增益已經穩定之後寫的部分：
    // 留下的最後一個樣本用的增益就是處理器現在的增益，接續處才連續
    const qint64 maxFrames = m_writtenFrames - std::max(m_trackWriteStart, m_gainSettledAt);
    if (m_activeDsdSink || maxFrames <= 0) {
        return true;
    }
    m_unwriteKeep.store(framesPerSecond(m_status.outputFormat) * kVolumeKeepMs / 1000,
                        std::memory_order_relaxed);
    m_unwriteMax.store(maxFrames, std::memory_order_relaxed);
    m_unwriteRequest.store(true, std::memory_order_release);
    // 寫入位置由音訊執行緒往回移，這段時間這裡不寫緩衝
    while (m_unwriteRequest.load(std::memory_order_acquire)) {
        if (m_stopDecoding.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const qint64 dropped = m_unwritten.load(std::memory_order_relaxed);
    if (dropped <= 0) {
        return true;
    }
    // 音源移回收回的那一段的開頭；seek 之後的樣本和從頭解碼相同，所以重做的只有增益不同
    const qint64 target = m_sourceFrame - dropped;
    auto sought = m_source->seek(target);
    if (!sought) {
        m_error = sought.error();
        return false;
    }
    m_sourceFrame = target;
    m_writtenFrames -= dropped;
    if (m_segmentLeft >= 0) {
        m_segmentLeft += dropped;
    }
    if (m_expectedLeft >= 0) {
        m_expectedLeft += dropped;
    }
    // 這首原本已經讀到結尾、等著換格式重開裝置時：收回的部分讀完會再標記一次
    m_reopenPending = false;
    return true;
}

void PlaybackEngine::serveUnwrite() noexcept {
    // 即時路徑：只碰環形緩衝與原子變數
    const qint64 readable = static_cast<qint64>(m_ring->readable()) / m_outputFrameBytes;
    qint64 drop = readable - m_unwriteKeep.load(std::memory_order_relaxed);
    const qint64 most = m_unwriteMax.load(std::memory_order_relaxed);
    drop = drop > most ? most : drop;
    if (drop > 0) {
        m_ring->unwrite(static_cast<std::size_t>(drop * m_outputFrameBytes));
    } else {
        drop = 0;
    }
    m_unwritten.store(drop, std::memory_order_relaxed);
    m_unwriteRequest.store(false, std::memory_order_release);
}

} // namespace auralis::core
