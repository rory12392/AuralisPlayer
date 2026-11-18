#include "pcm_convert_sink.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace auralis::mod::out {

namespace {

/// 濾波器規格（自己設計，Kaiser 窗法）：通帶邊緣、阻帶邊緣、阻帶衰減。
/// 阻帶從 60 kHz 起：176.4 kHz 輸出時會折疊回 0–20 kHz 的是 156 kHz 以上的成分，衰減遠大於 100 dB；
/// 20–60 kHz 是過渡帶（−6 dB 在 40 kHz），DSD 在那裡的雜訊大部分也被壓掉。
constexpr double kPassHz = 20000.0;
constexpr double kStopHz = 60000.0;
constexpr double kAttenuationDb = 100.0;
/// DSD 靜音碼（第一個位元在最高位）：一半 1、一半 0，濾波後接近 0。
constexpr std::uint8_t kDsdSilence = 0x69;
/// 24 位元滿刻度。
constexpr double kFullScale = 8388608.0;

/// 第一類修正貝索函數 I0（Kaiser 窗用），級數展開到收斂。
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double half = x / 2.0;
    for (int k = 1; k < 200; ++k) {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

/// Kaiser 窗的 sinc 低通（線性相位、對稱），長度取 8 的倍數，係數總和正規化成 1（直流增益 0 dB）。
/// 長度與 beta 用 Kaiser 的經驗公式：N ≈ (A − 8) ÷ (2.285 × 2π × 過渡帶寬 ÷ fs)、
/// beta = 0.1102 (A − 8.7)。
std::vector<double> designLowPass(double sampleRate) {
    const double width = 2.0 * std::numbers::pi * (kStopHz - kPassHz) / sampleRate;
    int taps = static_cast<int>(std::ceil((kAttenuationDb - 8.0) / (2.285 * width))) + 1;
    taps = (taps + 7) / 8 * 8;
    const double beta = 0.1102 * (kAttenuationDb - 8.7);
    const double cutoff = (kPassHz + kStopHz) / 2.0 / sampleRate; // 以取樣率為 1 的頻率
    const double center = (taps - 1) / 2.0;
    std::vector<double> h(static_cast<std::size_t>(taps));
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - center;
        const double x = 2.0 * std::numbers::pi * cutoff * m;
        const double sinc = m == 0.0 ? 2.0 * cutoff : std::sin(x) / (std::numbers::pi * m);
        const double r = m / center;
        const double window =
            besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        h[static_cast<std::size_t>(i)] = sinc * window;
        sum += h[static_cast<std::size_t>(i)];
    }
    for (double& c : h) {
        c /= sum;
    }
    return h;
}

} // namespace

PcmConvertSink::PcmConvertSink(quint32 outputRate) : m_outputRate(outputRate) {}

QString PcmConvertSink::name() const {
    return QStringLiteral("轉 PCM");
}

Result<AudioFormat> PcmConvertSink::open(const DsdFormat& format) {
    if (!format.isValid()) {
        return Error{QStringLiteral("不支援的 DSD 格式"), 0,
                     QStringLiteral("PcmConvertSink::open")};
    }
    // 一個輸出樣本要對齊整數個位元組（查表以位元組為單位），所以抽取倍數要是 8 的倍數
    if (m_outputRate == 0 || format.dsdRate % m_outputRate != 0 ||
        (format.dsdRate / m_outputRate) % 8 != 0) {
        return Error{QStringLiteral("無法把 %1 Hz 的 DSD 轉成 %2 Hz 的 PCM")
                         .arg(format.dsdRate)
                         .arg(m_outputRate),
                     0, QStringLiteral("PcmConvertSink::open")};
    }
    m_channels = format.channels;
    m_bytesPerOutput = static_cast<int>(format.dsdRate / m_outputRate / 8);

    // 每 8 個係數對一個位元組事先加好：第 g 組是倒數第 g 個位元組（g = 0 最新），
    // 位元組裡最高位是最早的位元，所以它的係數索引是 8g + 7，最低位是 8g
    const std::vector<double> h = designLowPass(static_cast<double>(format.dsdRate));
    m_groups = static_cast<int>(h.size() / 8);
    m_table.assign(static_cast<std::size_t>(m_groups) * 256, 0.0);
    for (int g = 0; g < m_groups; ++g) {
        for (int value = 0; value < 256; ++value) {
            double sum = 0.0;
            for (int bit = 0; bit < 8; ++bit) { // bit 0 是最高位
                const double tap = h[static_cast<std::size_t>(8 * g + 7 - bit)];
                sum += ((value >> (7 - bit)) & 1) != 0 ? tap : -tap;
            }
            m_table[static_cast<std::size_t>(g) * 256 + static_cast<std::size_t>(value)] = sum;
        }
    }
    // 歷史先填滿靜音碼：曲目從靜音開始時輸出一開始就是穩態，沒有濾波器的暖機暫態
    m_history.assign(static_cast<std::size_t>(m_channels) * 2 * static_cast<std::size_t>(m_groups),
                     kDsdSilence);
    m_write = 0;
    m_pending = 0;

    AudioFormat out;
    out.rate = m_outputRate;
    out.bits = 24;
    out.container = 32;
    out.channels = format.channels;
    return out;
}

qint64 PcmConvertSink::maxOutputFrames(qint64 dsdFrames) const noexcept {
    if (m_bytesPerOutput == 0) {
        return dsdFrames * 4;
    }
    // 上一次留下最多 m_bytesPerOutput − 1 個位元組，所以向上取整
    return (dsdFrames * 4 + m_bytesPerOutput - 1) / m_bytesPerOutput;
}

qint64 PcmConvertSink::convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept {
    // 輸入：每聲道 4 個 DSD 位元組（時間順序）、聲道交錯。每收滿 m_bytesPerOutput 個位元組，
    // 每個聲道算一個輸出樣本：24 位元左對齊在 32 位元容器（小端：0、低、中、高）
    const auto groups = static_cast<std::size_t>(m_groups);
    qint64 produced = 0;
    for (qint64 frame = 0; frame < dsdFrames; ++frame) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(dsd + frame * m_channels * 4);
        for (int k = 0; k < 4; ++k) {
            m_write = m_write + 1 == m_groups ? 0 : m_write + 1;
            for (int channel = 0; channel < m_channels; ++channel) {
                std::uint8_t* history =
                    m_history.data() + static_cast<std::size_t>(channel) * 2 * groups;
                const std::uint8_t value = in[channel * 4 + k];
                history[m_write] = value;
                history[static_cast<std::size_t>(m_write) + groups] = value;
            }
            if (++m_pending < m_bytesPerOutput) {
                continue;
            }
            m_pending = 0;
            for (int channel = 0; channel < m_channels; ++channel) {
                const std::uint8_t* newest = m_history.data() +
                                             static_cast<std::size_t>(channel) * 2 * groups +
                                             static_cast<std::size_t>(m_write) + groups;
                const double scaled = std::nearbyint(filterOutput(newest) * kFullScale);
                const auto sample =
                    static_cast<std::int32_t>(std::clamp(scaled, -kFullScale, kFullScale - 1.0));
                const auto word = static_cast<std::uint32_t>(sample) << 8;
                out[0] = std::byte{0};
                out[1] = static_cast<std::byte>(word >> 8);
                out[2] = static_cast<std::byte>(word >> 16);
                out[3] = static_cast<std::byte>(word >> 24);
                out += 4;
            }
            ++produced;
        }
    }
    return produced;
}

QString PcmConvertSink::reason() const {
    return QStringLiteral("DSD 轉 PCM");
}

int PcmConvertSink::taps() const noexcept {
    return m_groups * 8;
}

double PcmConvertSink::filterOutput(const std::uint8_t* newest) const noexcept {
    // newest 指向最新的位元組；往前（位址遞減）依序是更舊的，最近 m_groups 個位元組在記憶體裡連續
    double sum = 0.0;
    const double* table = m_table.data();
    for (int g = 0; g < m_groups; ++g) {
        sum += table[static_cast<std::size_t>(g) * 256 + newest[-g]];
    }
    return sum;
}

} // namespace auralis::mod::out
