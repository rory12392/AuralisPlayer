#pragma once

#include <auralis/dsd.h>

namespace auralis::mod::out {

/// DoP（DSD over PCM）包裝器：把 DSD 位元流包成 24 位元 PCM 樣本，交給核心的 WASAPI 獨占後端。
/// 每個 24 位元樣本 = 標記位元組（0x05／0xFA 每個樣本框交替）＋ 16 個 DSD 位元（時間先的在高位），
/// 放在 32 位元容器裡左對齊；取樣率是 DSD 取樣率 ÷ 16（DSD64 → 176.4 kHz）。
/// 不改變位元流（DAC 收到後還原成原本的 DSD），所以 reason() 是空字串。
/// 執行緒：全部在核心的解碼執行緒呼叫（open 與 convert 不同時）。
class DopSink final : public DsdSink {
public:
    QString name() const override;
    Result<AudioFormat> open(const DsdFormat& format) override;
    /// 每個 DSD 樣本框（每聲道 32 位元）變成兩個 DoP 樣本框，標記依序是 0x05、0xFA；
    /// 一個 DSD 樣本框剛好用掉一組標記，所以分幾次轉換、曲目相接時標記都自然接續，不用記狀態。
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override;
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override;

private:
    int m_channels = 0;
};

} // namespace auralis::mod::out
