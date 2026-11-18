#pragma once

// 音量測試（AC-02-7）共用的探測訊號與分析：
// 左聲道是固定的半刻度（輸出 ÷ 輸入就是那個樣本框的增益），
// 右聲道是雜訊（確認樣本沒有錯位）。

#include "audio/capture_backend.h"
#include "audio/next_track.h"
#include "audio/volume_processor.h"
#include "signal_writer.h"

#include <QByteArray>
#include <QtEndian>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <thread>
#include <vector>

namespace auralis::test {

inline AudioFormat pcmFormat(quint32 rate, quint16 bits) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    return format;
}

/// 第 index 個樣本（交錯）的值，單位是容器的整數。
inline qint64 sampleAt(const QByteArray& bytes, const AudioFormat& format, qsizetype index) {
    if (format.container == 16) {
        return qFromLittleEndian<qint16>(bytes.constData() + index * 2);
    }
    return qFromLittleEndian<qint32>(bytes.constData() + index * 4);
}

/// 左聲道的固定值：半刻度。
inline qint64 halfScale(const AudioFormat& format) {
    return format.container == 16 ? 0x4000 : 0x40000000;
}

/// 一個最低位元在容器整數裡的大小。
inline qint64 lsb(const AudioFormat& format) {
    return qint64{1} << (format.container - format.bits);
}

/// 探測訊號：左聲道固定半刻度、右聲道雜訊。
inline QByteArray makeProbe(const AudioFormat& format, int frames, quint32 seed) {
    QByteArray data = makeNoise(format, frames, seed);
    for (int i = 0; i < frames; ++i) {
        char* left = data.data() + static_cast<qsizetype>(i) * format.bytesPerFrame();
        if (format.container == 16) {
            qToLittleEndian<qint16>(static_cast<qint16>(halfScale(format)), left);
        } else {
            qToLittleEndian<qint32>(static_cast<qint32>(halfScale(format)), left);
        }
    }
    return data;
}

/// 輸出對照輸入的分析結果。
struct Analysis {
    /// 每個樣本框的增益（左聲道輸出 ÷ 輸入）。
    std::vector<double> gain;
    /// 第一個、最後一個和輸入不同的樣本框；都相同時 -1。
    qint64 firstChanged = -1;
    qint64 lastChanged = -1;
    /// 相鄰兩個樣本框的增益最多差多少。
    double maxStep = 0.0;
    /// 右聲道和「輸入 × 這個樣本框的增益」最多差幾個最低位元（扣掉增益估計本身的誤差）。
    double worstAlignment = 0.0;
};

inline Analysis analyze(const QByteArray& input, const QByteArray& output,
                        const AudioFormat& format) {
    Analysis result;
    const qint64 frames = input.size() / format.bytesPerFrame();
    const double half = static_cast<double>(halfScale(format));
    const double unit = static_cast<double>(lsb(format));
    result.gain.reserve(static_cast<std::size_t>(frames));
    for (qint64 i = 0; i < frames; ++i) {
        const qint64 inRight = sampleAt(input, format, i * 2 + 1);
        const qint64 outLeft = sampleAt(output, format, i * 2);
        const qint64 outRight = sampleAt(output, format, i * 2 + 1);
        const double gain = static_cast<double>(outLeft) / half;
        if (!result.gain.empty()) {
            result.maxStep = std::max(result.maxStep, std::abs(gain - result.gain.back()));
        }
        result.gain.push_back(gain);
        if (outLeft != halfScale(format) || outRight != inRight) {
            result.firstChanged = result.firstChanged < 0 ? i : result.firstChanged;
            result.lastChanged = i;
        }
        // 增益是由抖動過的左聲道估的，本身最多差 2 個最低位元 ÷ 半刻度
        const double slack = std::abs(static_cast<double>(inRight)) * 2.0 * unit / half;
        const double error =
            std::abs(static_cast<double>(outRight) - gain * static_cast<double>(inRight)) - slack;
        result.worstAlignment = std::max(result.worstAlignment, error / unit);
    }
    return result;
}

/// 相鄰樣本框的增益差的上限：最陡的斜坡（0 ↔ 1 走完 30 毫秒）一步的量，加上抖動造成的估計誤差。
inline double stepLimit(const AudioFormat& format) {
    const double rampFrames = format.rate * core::VolumeProcessor::kRampMs / 1000.0;
    return 1.0 / rampFrames +
           4.0 * static_cast<double>(lsb(format)) / static_cast<double>(halfScale(format));
}

/// 有沒有一段（至少 minFrames 個樣本框）增益穩定在 gain。
inline bool hasPlateau(const Analysis& analysis, double gain, const AudioFormat& format,
                       qint64 minFrames) {
    const double tolerance =
        3.0 * static_cast<double>(lsb(format)) / static_cast<double>(halfScale(format));
    qint64 run = 0;
    for (const double g : analysis.gain) {
        run = std::abs(g - gain) <= tolerance ? run + 1 : 0;
        if (run >= minFrames) {
            return true;
        }
    }
    return false;
}

/// 實際時間的裝置節奏（不加速）：收回緩衝重做要在留下的 0.1 秒內完成，加速會把這段時間縮短。
inline core::CaptureBackend::Options realtimeCapture() {
    core::CaptureBackend::Options options;
    options.speed = 1.0;
    options.maxCaptureSeconds = 10;
    return options;
}

inline void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/// 依序交出事先準備好的曲目。
class ListProvider final : public core::NextTrackProvider {
public:
    std::optional<core::EngineTrack> nextTrack() override {
        if (tracks.empty()) {
            return std::nullopt;
        }
        core::EngineTrack track = std::move(tracks.front());
        tracks.pop_front();
        return track;
    }
    std::deque<core::EngineTrack> tracks;
};

} // namespace auralis::test
