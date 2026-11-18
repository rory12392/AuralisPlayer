#pragma once

#include <QtGlobal>

namespace auralis {

/// 音源交出的 PCM 格式。樣本一律交錯（LRLR…）、小端序。
/// 整數樣本左對齊放在容器裡：24 位元的來源是 container=32、bits=24，低 8 位元為 0；
/// 這樣不同位元深度可以用同一條路徑處理，輸出後端再依裝置打包（24-in-24 或 24-in-32）。
/// 浮點樣本是 container=32、bits=32、isFloat=true（32 位元浮點 WAV 與有損格式）。
/// dsdRate 只有 DSD 位元流才不是 0（例：DSD64 為 2822400），那時 rate 是 0。
/// 執行緒：值型別。
struct AudioFormat {
    /// 取樣率（Hz）。
    quint32 rate = 0;
    /// 有效位元數（16、24、32）。
    quint16 bits = 0;
    /// 每個樣本在記憶體中佔的位元數（16 或 32）。
    quint16 container = 0;
    quint16 channels = 0;
    bool isFloat = false;
    quint32 dsdRate = 0;

    /// 每個樣本框（所有聲道各一個樣本）的位元組數。
    int bytesPerFrame() const noexcept { return channels * (container / 8); }
    bool isValid() const noexcept {
        return channels > 0 && (container == 16 || container == 32) && bits <= container &&
               (rate > 0 || dsdRate > 0);
    }

    friend bool operator==(const AudioFormat&, const AudioFormat&) = default;
};

} // namespace auralis
