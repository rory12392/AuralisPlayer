#pragma once

#include <auralis/audio_source.h>

namespace auralis::mod::out {

/// DSF（.dsf）與 DSDIFF（.dff）的音源工廠：交出核心約定的 DSD 位元流
/// （每聲道 32 個位元為一個樣本框、聲道以 4 位元組交錯、最高位元在前）。
/// 執行緒：open() 在解碼執行緒呼叫；工廠本身沒有狀態。
class DsdSourceFactory final : public ISourceFactory {
public:
    QString name() const override;
    QStringList extensions() const override;
    Result<std::unique_ptr<IAudioSource>> open(const QString& path) override;
};

} // namespace auralis::mod::out
