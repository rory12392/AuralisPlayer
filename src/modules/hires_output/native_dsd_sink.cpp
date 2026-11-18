#include "native_dsd_sink.h"

#include <cstring>

namespace auralis::mod::out {

QString NativeDsdSink::name() const {
    return QStringLiteral("原生 DSD");
}

Result<AudioFormat> NativeDsdSink::open(const DsdFormat& format) {
    if (!format.isValid()) {
        return Error{QStringLiteral("不支援的 DSD 格式"), 0, QStringLiteral("NativeDsdSink::open")};
    }
    m_frameBytes = format.bytesPerFrame();
    // 交給後端的就是音源的 DSD 約定（dsdRate、container 32、bits 1）
    AudioFormat out;
    out.dsdRate = format.dsdRate;
    out.bits = 1;
    out.container = 32;
    out.channels = format.channels;
    return out;
}

qint64 NativeDsdSink::maxOutputFrames(qint64 dsdFrames) const noexcept {
    return dsdFrames;
}

qint64 NativeDsdSink::convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept {
    std::memcpy(out, dsd, static_cast<std::size_t>(dsdFrames * m_frameBytes));
    return dsdFrames;
}

} // namespace auralis::mod::out
