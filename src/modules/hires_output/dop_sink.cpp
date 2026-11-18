#include "dop_sink.h"

namespace auralis::mod::out {

namespace {

/// DoP 的標記位元組：相鄰樣本框交替，DAC 靠它認出這是 DSD 而不是 PCM。
constexpr std::byte kMarkerFirst{0x05};
constexpr std::byte kMarkerSecond{0xFA};

} // namespace

QString DopSink::name() const {
    return QStringLiteral("DoP");
}

Result<AudioFormat> DopSink::open(const DsdFormat& format) {
    if (!format.isValid()) {
        return Error{QStringLiteral("不支援的 DSD 格式"), 0, QStringLiteral("DopSink::open")};
    }
    m_channels = format.channels;
    // 每個 PCM 樣本帶 16 個 DSD 位元，所以取樣率是 DSD 的 1/16（DSD256 → 705.6 kHz，
    // 裝置不支援時由輸出後端回報格式不支援）
    AudioFormat out;
    out.rate = format.dsdRate / 16;
    out.bits = 24;
    out.container = 32;
    out.channels = format.channels;
    return out;
}

qint64 DopSink::maxOutputFrames(qint64 dsdFrames) const noexcept {
    return dsdFrames * 2;
}

qint64 DopSink::convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept {
    // 輸入：每聲道 4 個 DSD 位元組（時間順序）、聲道交錯。輸出：小端 32 位元容器、24 位元左對齊，
    // 位元組依序是 0、後 8 個 DSD 位元、前 8 個 DSD 位元、標記。
    for (qint64 frame = 0; frame < dsdFrames; ++frame) {
        const std::byte* in = dsd + frame * m_channels * 4;
        for (int half = 0; half < 2; ++half) {
            const std::byte marker = half == 0 ? kMarkerFirst : kMarkerSecond;
            for (int channel = 0; channel < m_channels; ++channel) {
                const std::byte* bits = in + channel * 4 + half * 2;
                out[0] = std::byte{0};
                out[1] = bits[1];
                out[2] = bits[0];
                out[3] = marker;
                out += 4;
            }
        }
    }
    return dsdFrames * 2;
}

} // namespace auralis::mod::out
