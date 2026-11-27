#pragma once

// DSD 路徑的測試共用：不記狀態的 DoP 包裝器。

#include <auralis/dsd.h>

namespace auralis::test {

/// 測試用的 DoP 包裝器（不記狀態）：每個 DSD 樣本框變成兩個 24-in-32 樣本框，
/// 標記依序是 0x05、0xFA，下面接 16 個 DSD 位元（先到的 8 位元在高位）。
class StatelessDopSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("DoP（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
        m_channels = format.channels;
        AudioFormat out;
        out.rate = format.dsdRate / 16;
        out.bits = 24;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames * 2; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        const auto* in = reinterpret_cast<const quint8*>(dsd);
        auto* samples = reinterpret_cast<quint32*>(out);
        for (qint64 f = 0; f < dsdFrames; ++f) {
            for (int half = 0; half < 2; ++half) {
                const quint32 mark = half == 0 ? 0x05u : 0xFAu;
                for (int ch = 0; ch < m_channels; ++ch) {
                    const quint8* bytes = in + (f * m_channels + ch) * 4 + half * 2;
                    *samples++ =
                        (mark << 24) | (quint32{bytes[0]} << 16) | (quint32{bytes[1]} << 8);
                }
            }
        }
        return dsdFrames * 2;
    }

private:
    int m_channels = 0;
};

} // namespace auralis::test
