#pragma once

// DoP 測試共用：依 DoP 規格逐樣本拆解封包（不用被測程式算預期值）、格式、曲目清單。

#include "engine_helpers.h"

#include "audio/next_track.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>

namespace auralis::test {

/// 換格式時頭尾各送的靜音下限（毫秒，AC-04-6、模組規格的格式切換）。
inline constexpr int kDopSilenceMs = 50;
/// DSD 的靜音位元組。
inline constexpr char kDsdSilence = 0x69;

inline AudioFormat dsdAudio(quint32 rate, quint16 channels = 2) {
    AudioFormat format;
    format.dsdRate = rate;
    format.bits = 1;
    format.container = 32;
    format.channels = channels;
    return format;
}

inline AudioFormat pcmFormat(quint32 rate, quint16 bits) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    return format;
}

/// 拆解 DoP 封包的結果。
struct DopParse {
    /// 空字串表示每個樣本都符合 DoP；否則是第一個不符合的地方。
    QString problem;
    /// 第一個樣本框的標記。
    quint8 firstMarker = 0;
    /// 還原出來的 DSD 位元流（核心約定的排列：每聲道 4 位元組、聲道交錯）。
    QByteArray dsd;
};

/// 依 DoP 規格逐個樣本檢查：樣本是小端、左對齊在 sampleBytes 個位元組裡，
/// 最高位元組是標記，同一個樣本框各聲道標記相同、相鄰樣本框 0x05／0xFA 交替，
/// 接著兩個位元組是 16 個 DSD 位元（時間先的在高位），32 位元容器的最低位元組是 0。
inline DopParse parseDop(const QByteArray& data, int channels, int sampleBytes = 4) {
    DopParse result;
    const qsizetype frameBytes = static_cast<qsizetype>(channels) * sampleBytes;
    const qsizetype frames = data.size() / frameBytes;
    if (data.size() % frameBytes != 0 || frames % 2 != 0) {
        result.problem = QStringLiteral("長度不是成對的 DoP 樣本框：%1 位元組").arg(data.size());
        return result;
    }
    const auto byteAt = [&](qsizetype frame, int channel, int index) {
        return static_cast<quint8>(data[frame * frameBytes + channel * sampleBytes + index]);
    };
    // 32 位元容器：[0] 是 0、[1] 是後 8 個 DSD 位元、[2] 是前 8 個、[3] 是標記；24 位元少了 [0]
    const int low = sampleBytes - 3;
    result.dsd.reserve(frames / 2 * channels * 4);
    QByteArray pair(channels * 4, '\0');
    for (qsizetype f = 0; f < frames; ++f) {
        const quint8 marker = byteAt(f, 0, sampleBytes - 1);
        if (f == 0) {
            result.firstMarker = marker;
        }
        const quint8 expected =
            f == 0 ? marker : (byteAt(f - 1, 0, sampleBytes - 1) == 0x05 ? 0xFA : 0x05);
        if ((marker != 0x05 && marker != 0xFA) || marker != expected) {
            result.problem =
                QStringLiteral("第 %1 框的標記是 0x%2").arg(f).arg(marker, 2, 16, QChar('0'));
            return result;
        }
        for (int c = 0; c < channels; ++c) {
            if (byteAt(f, c, sampleBytes - 1) != marker) {
                result.problem =
                    QStringLiteral("第 %1 框第 %2 聲道的標記和第 1 聲道不同").arg(f).arg(c);
                return result;
            }
            if (sampleBytes == 4 && byteAt(f, c, 0) != 0) {
                result.problem =
                    QStringLiteral("第 %1 框第 %2 聲道的最低位元組不是 0").arg(f).arg(c);
                return result;
            }
            const int half = static_cast<int>(f % 2);
            pair[c * 4 + half * 2] = static_cast<char>(byteAt(f, c, low + 1));
            pair[c * 4 + half * 2 + 1] = static_cast<char>(byteAt(f, c, low));
        }
        if (f % 2 == 1) {
            result.dsd.append(pair);
        }
    }
    return result;
}

/// 一段 DSD 位元流全是靜音位元組。
inline bool allDsdSilence(const QByteArray& dsd) {
    return !dsd.isEmpty() &&
           std::all_of(dsd.begin(), dsd.end(), [](char b) { return b == kDsdSilence; });
}

inline bool allZeroBytes(const QByteArray& data) {
    return std::all_of(data.begin(), data.end(), [](char b) { return b == 0; });
}

/// 依序交出事先準備好的曲目。
class TrackList final : public core::NextTrackProvider {
public:
    void add(std::unique_ptr<IAudioSource> source, quint64 token) {
        m_tracks.push_back(core::EngineTrack{std::move(source), 0, -1, token});
    }
    std::optional<core::EngineTrack> nextTrack() override {
        if (m_tracks.empty()) {
            return std::nullopt;
        }
        core::EngineTrack track = std::move(m_tracks.front());
        m_tracks.pop_front();
        return track;
    }

private:
    std::deque<core::EngineTrack> m_tracks;
};

/// 等到播完再停（換格式的測試要讀每一段裝置紀錄）。
inline bool playToEnd(core::PlaybackEngine& engine) {
    return waitFinished(engine, 30000);
}

} // namespace auralis::test
