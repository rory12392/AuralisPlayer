#pragma once

#include <auralis/dsd.h>

#include <cstdint>
#include <vector>

namespace auralis::mod::out {

/// 轉 PCM 包裝器（AC-04-5）：把 DSD 位元流用 FIR 低通濾波後抽取成 PCM，給收不了 DoP 與原生 DSD 的
/// 輸出用。交給後端的格式是整數 PCM、24 位元左對齊在 32 位元容器（和 DoP 相同的排列），
/// 取樣率預設 176.4 kHz（`OUT/dsdPcmRate`，由建構時給）。
///
/// 濾波器自己設計，不借用外部係數：Kaiser 窗的 sinc 低通，通帶到 20 kHz、阻帶從 60 kHz 起、
/// 阻帶衰減 100 dB 的設計值（實際 ≥ 98 dB），截止頻率（−6 dB）40 kHz；長度依 DSD 取樣率在 open()
/// 時算（DSD64 456、DSD128 912、DSD256 1816 個位元），係數總和正規化成 1。DSD 位元 1 → +1、0 → −1，
/// 增益 0 dB（DSD 滿調變對到 PCM 滿刻度），超出 24 位元範圍時限幅。
///
/// 因為輸入只有 ±1，每 8 個係數依位元組的 256 種值事先加好（查表），convert 每個輸出樣本只做
/// 「濾波器長度 ÷ 8」次查表與加法。濾波歷史在 open() 時填滿 DSD 靜音碼 0x69，所以靜音一開始就是
/// 穩態輸出（接近 0），曲目從靜音開始時不會有濾波器暖機的暫態。
///
/// 改變訊號，所以 reason() 是「DSD 轉 PCM」。執行緒：全部在核心的解碼執行緒呼叫
/// （open 與 convert 不同時）；convert 不配置記憶體、不丟例外。
class PcmConvertSink final : public DsdSink {
public:
    /// 預設輸出取樣率（AC-04-5）。
    static constexpr quint32 kDefaultRate = 176400;

    /// outputRate：輸出取樣率；DSD 取樣率要是它的 8 的倍數倍，否則 open() 回錯誤。
    explicit PcmConvertSink(quint32 outputRate = kDefaultRate);

    QString name() const override;
    Result<AudioFormat> open(const DsdFormat& format) override;
    /// 每聲道每 (DSD 取樣率 ÷ 輸出取樣率 ÷ 8) 個位元組產生一個輸出樣本框；不足一個的留到下一次。
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override;
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override;
    QString reason() const override;

    /// 目前的濾波器長度（DSD 位元數，open() 之後才有；給測試與日誌）。
    int taps() const noexcept;

private:
    double filterOutput(const std::uint8_t* newest) const noexcept;

    quint32 m_outputRate;
    int m_channels = 0;
    /// 每個輸出樣本用掉每聲道幾個 DSD 位元組。
    int m_bytesPerOutput = 0;
    /// 濾波器長度 ÷ 8（一組 8 個係數對一個位元組）。
    int m_groups = 0;
    /// m_groups × 256：第 g 組（g = 0 是最新的位元組）對每種位元組值的部分和。
    std::vector<double> m_table;
    /// 每聲道 2 × m_groups 個位元組：同一個位元組寫在 i 與 i + m_groups，
    /// 所以讀最近的 m_groups 個位元組時在記憶體裡連續。
    std::vector<std::uint8_t> m_history;
    int m_write = 0;
    int m_pending = 0;
};

} // namespace auralis::mod::out
