#pragma once

// 轉 PCM 測試共用：已知頻率的 DSD 測試音（二階三角積分調變，和硬體探針同一種調變器）、
// 解開 24-in-32 的 PCM 輸出、用最小平方法量指定頻率的振幅。預期值都在這裡獨立算，不用被測程式。

#include <QByteArray>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace auralis::test {

/// DSD 正弦產生器：每次給一個位元組（8 個 DSD 位元，時間先的在最高位）。
/// 振幅以 DSD 滿調變為 1；二階調變器在振幅 ≤ 0.5 時穩定。
class DsdTone {
public:
    DsdTone(double dsdRate, double hz, double amplitude)
        : m_cosStep(std::cos(2.0 * std::numbers::pi * hz / dsdRate)),
          m_sinStep(std::sin(2.0 * std::numbers::pi * hz / dsdRate)), m_amplitude(amplitude) {}

    std::uint8_t nextByte() {
        std::uint8_t byte = 0;
        for (int i = 0; i < 8; ++i) {
            // 用旋轉代替每個位元呼叫 sin（60 秒的測試有上億個位元）；每個位元組重新正規化一次
            const double input = m_amplitude * m_sin;
            const double c = m_cos * m_cosStep - m_sin * m_sinStep;
            m_sin = m_sin * m_cosStep + m_cos * m_sinStep;
            m_cos = c;
            m_x1 += input - m_y;
            m_x2 += m_x1 - m_y;
            m_y = m_x2 >= 0.0 ? 1.0 : -1.0;
            byte = static_cast<std::uint8_t>(byte | ((m_y > 0.0 ? 1 : 0) << (7 - i)));
        }
        const double norm = std::sqrt(m_sin * m_sin + m_cos * m_cos);
        m_sin /= norm;
        m_cos /= norm;
        return byte;
    }

private:
    double m_cosStep;
    double m_sinStep;
    double m_amplitude;
    double m_sin = 0.0;
    double m_cos = 1.0;
    double m_x1 = 0.0;
    double m_x2 = 0.0;
    double m_y = 1.0;
};

/// 2 聲道 DSD（核心約定的排列）：第 1 聲道是測試音，第 2 聲道是它逐位元反相（= 負的同一個訊號），
/// 用來確認聲道沒有排錯。
inline QByteArray stereoToneDsd(DsdTone& tone, qsizetype frames) {
    QByteArray out(frames * 8, '\0');
    for (qsizetype f = 0; f < frames; ++f) {
        for (int k = 0; k < 4; ++k) {
            const std::uint8_t b = tone.nextByte();
            out[f * 8 + k] = static_cast<char>(b);
            out[f * 8 + 4 + k] = static_cast<char>(~b);
        }
    }
    return out;
}

/// 24 位元左對齊在 32 位元容器（小端）的樣本 → 整數；lowByteOk 記下最低位元組是否都是 0。
inline std::vector<std::int32_t> unpack24in32(const QByteArray& data, bool* lowByteOk = nullptr) {
    std::vector<std::int32_t> samples(static_cast<std::size_t>(data.size() / 4));
    bool ok = true;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(data.constData()) + i * 4;
        ok = ok && p[0] == 0;
        const std::uint32_t raw =
            static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        samples[i] = static_cast<std::int32_t>(raw) >> 8;
    }
    if (lowByteOk != nullptr) {
        *lowByteOk = ok;
    }
    return samples;
}

/// 交錯的樣本取出一個聲道，並換成以滿刻度為 1 的值。
inline std::vector<double> channelOf(const std::vector<std::int32_t>& interleaved, int channels,
                                     int channel) {
    std::vector<double> out;
    for (std::size_t i = static_cast<std::size_t>(channel); i < interleaved.size();
         i += static_cast<std::size_t>(channels)) {
        out.push_back(interleaved[i] / 8388608.0);
    }
    return out;
}

/// 用最小平方法擬合 hz 的正弦，回傳振幅與擬合後的殘差均方根（滿刻度為 1）。
struct ToneFit {
    double amplitude = 0.0;
    double residualRms = 0.0;
};

inline ToneFit fitTone(const std::vector<double>& x, double rate, double hz) {
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double w = 2.0 * std::numbers::pi * hz * static_cast<double>(n) / rate;
        const double s = std::sin(w);
        const double c = std::cos(w);
        ss += s * s;
        cc += c * c;
        sc += s * c;
        xs += x[n] * s;
        xc += x[n] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det;
    const double b = (xc * ss - xs * sc) / det;
    double residual = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double w = 2.0 * std::numbers::pi * hz * static_cast<double>(n) / rate;
        const double r = x[n] - a * std::sin(w) - b * std::cos(w);
        residual += r * r;
    }
    return {std::hypot(a, b), std::sqrt(residual / static_cast<double>(x.size()))};
}

/// 由負到正的過零次數（估頻率用）。
inline int risingZeroCrossings(const std::vector<double>& x) {
    int count = 0;
    for (std::size_t n = 1; n < x.size(); ++n) {
        count += (x[n - 1] < 0.0 && x[n] >= 0.0) ? 1 : 0;
    }
    return count;
}

inline double toDb(double value) {
    return 20.0 * std::log10(value);
}

} // namespace auralis::test
