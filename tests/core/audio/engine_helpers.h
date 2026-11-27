#pragma once

// 播放引擎測試共用：記憶體裡的音源、播到結尾再停。

#include "audio/capture_backend.h"
#include "audio/playback_engine.h"

#include <auralis/audio_source.h>

#include <QByteArray>

#include <chrono>
#include <cstring>
#include <map>
#include <thread>

namespace auralis::test {

/// CD 格式：44.1 kHz、16 位元、雙聲道。
inline AudioFormat cd() {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    return format;
}

/// 從一段記憶體交出樣本的音源（不經 FFmpeg），格式由測試指定。
class MemorySource final : public IAudioSource {
public:
    MemorySource(AudioFormat format, QByteArray data) : m_format(format), m_data(std::move(data)) {}

    AudioFormat format() const override { return m_format; }
    qint64 lengthFrames() const override { return m_data.size() / m_format.bytesPerFrame(); }
    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const qint64 frameBytes = m_format.bytesPerFrame();
        const qint64 left = (m_data.size() - m_pos) / frameBytes;
        const qint64 frames = maxFrames < left ? maxFrames : left;
        std::memcpy(dest, m_data.constData() + m_pos,
                    static_cast<std::size_t>(frames * frameBytes));
        m_pos += frames * frameBytes;
        return frames;
    }
    Result<qint64> seek(qint64 frame) override {
        m_pos = frame * m_format.bytesPerFrame();
        return frame;
    }

private:
    AudioFormat m_format;
    QByteArray m_data;
    qint64 m_pos = 0;
};

/// 副檔名 .mem 的音源，格式由測試指定（tracks 的鍵是路徑）。
class MemoryFactory final : public ISourceFactory {
public:
    QString name() const override { return QStringLiteral("記憶體"); }
    QStringList extensions() const override { return {QStringLiteral("mem")}; }
    Result<std::unique_ptr<IAudioSource>> open(const QString& path) override {
        const auto& [format, data] = tracks.at(path);
        return std::unique_ptr<IAudioSource>(std::make_unique<MemorySource>(format, data));
    }
    std::map<QString, std::pair<AudioFormat, QByteArray>> tracks;
};

/// 等引擎把曲目送完（最多 timeoutMs 毫秒）再停；回傳有沒有在時限內送完。
inline bool waitFinished(core::PlaybackEngine& engine, int timeoutMs = 20000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool finished = engine.isFinished();
    engine.stop();
    return finished;
}

/// 測試用的 Capture 設定：4 倍速、記錄 10 秒。
inline core::CaptureBackend::Options fastCapture() {
    core::CaptureBackend::Options options;
    options.speed = 4.0;
    options.maxCaptureSeconds = 10;
    return options;
}

/// captured 的開頭是否就是 expected，之後全是 0（曲目結束後補的靜音）。
inline bool startsWithThenSilence(const QByteArray& captured, const QByteArray& expected) {
    if (captured.size() < expected.size() || captured.left(expected.size()) != expected) {
        return false;
    }
    for (qsizetype i = expected.size(); i < captured.size(); ++i) {
        if (captured[i] != 0) {
            return false;
        }
    }
    return true;
}

} // namespace auralis::test
