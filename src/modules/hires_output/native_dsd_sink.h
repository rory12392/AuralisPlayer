#pragma once

#include <auralis/dsd.h>

namespace auralis::mod::out {

/// 原生 DSD 包裝器：不包裝，把 DSD 位元流原樣交給輸出後端（格式就是 DSD 格式本身）。
/// 只有 ASIO 後端收得下：它用 kAsioSetIoFormat 切到原生 DSD，驅動不接受時自己改送 DoP
/// （AC-04-2、AC-04-4）；WASAPI 後端收到 DSD 格式會回報不支援。
/// 不改變位元流，所以 reason() 是空字串。執行緒：全部在核心的解碼執行緒呼叫。
class NativeDsdSink final : public DsdSink {
public:
    QString name() const override;
    Result<AudioFormat> open(const DsdFormat& format) override;
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override;
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override;

private:
    int m_frameBytes = 0;
};

} // namespace auralis::mod::out
