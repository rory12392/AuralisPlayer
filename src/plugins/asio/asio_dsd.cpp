#include "asio_dsd.h"

#include "asio_backend.h"

#include <windows.h>

#include <objbase.h>

#include <asiosys.h>

#include <asio.h>
#include <iasiodrv.h>

#include <QLoggingCategory>

#include <cstring>

Q_DECLARE_LOGGING_CATEGORY(lcAsio)

namespace auralis::mod::out {

bool AsioBackend::tryNativeDsd(quint32 dsdRate, long channels) {
    // 只看 kAsioSetIoFormat 的結果（T-30：kAsioCanDoIoFormat 與 canSampleRate 都不可信）；
    // PCM 狀態下設 DSD 位元率會回 ASE_NoClock，所以先切格式再設位元率
    if (!setAsioIoFormat(m_driver, true)) {
        qCWarning(lcAsio).noquote()
            << "ASIO 驅動不接受 kAsioSetIoFormat(DSD)，改送 DoP：" << m_entry.name;
        return false;
    }
    m_ioFormatDsd = true;
    const auto giveUp = [this](const char* why) {
        qCWarning(lcAsio).noquote()
            << "ASIO 原生 DSD 無法使用（" << why << "），改送 DoP：" << m_entry.name;
        m_ioFormatDsd = !setAsioIoFormat(m_driver, false);
        return false;
    };
    if (m_driver->setSampleRate(static_cast<ASIOSampleRate>(dsdRate)) != ASE_OK) {
        return giveUp("setSampleRate");
    }
    for (long channel = 0; channel < channels; ++channel) {
        ASIOChannelInfo info{};
        info.channel = channel;
        info.isInput = ASIOFalse;
        if (m_driver->getChannelInfo(&info) != ASE_OK || info.type != ASIOSTDSDInt8MSB1) {
            return giveUp("聲道格式不是 DSDInt8MSB1");
        }
    }
    return true;
}

bool setAsioIoFormat(IASIO* driver, bool dsd) noexcept {
    ASIOIoFormat format{};
    format.FormatType = dsd ? kASIODSDFormat : kASIOPCMFormat;
    return driver->future(kAsioSetIoFormat, &format) == ASE_SUCCESS;
}

void deinterleaveDsdToAsio(const std::byte* interleaved, int channels, qint64 dsdFrames,
                           void* const* outputs, int outputCount) noexcept {
    const int frameBytes = channels * 4;
    for (int channel = 0; channel < outputCount; ++channel) {
        const int source = channels == 1 ? 0 : channel;
        auto* out = static_cast<std::byte*>(outputs[channel]);
        const std::byte* in = interleaved + source * 4;
        for (qint64 f = 0; f < dsdFrames; ++f, in += frameBytes, out += 4) {
            std::memcpy(out, in, 4);
        }
    }
}

} // namespace auralis::mod::out
