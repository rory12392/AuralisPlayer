#include "audio/wasapi_format.h"

#include <audioclient.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace auralis::core {

namespace {

// KSDATAFORMAT_SUBTYPE_PCM／IEEE_FLOAT：自己定義，避免 ksmedia.h 的 GUID 要另外連結
constexpr GUID kSubtypePcm{
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kSubtypeFloat{
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

/// HRESULT_FROM_WIN32(ERROR_NOT_FOUND)：IMMDeviceEnumerator::GetDevice 找不到端點（裝置已移除）。
/// 那個巨集在新的 SDK 是 inline 函式，不能放在 case 裡。
constexpr HRESULT kNotFound = static_cast<HRESULT>(0x80070490L);

AudioFormat withLayout(const AudioFormat& base, quint16 bits, quint16 container, bool isFloat) {
    AudioFormat format = base;
    format.bits = bits;
    format.container = container;
    format.isFloat = isFloat;
    return format;
}

DWORD channelMask(int channels) {
    switch (channels) {
    case 1:
        return SPEAKER_FRONT_CENTER;
    case 2:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    case 4:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 6:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
               SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
    case 8:
        return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
               SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT |
               SPEAKER_SIDE_RIGHT;
    default:
        return 0;
    }
}

/// 讀一個整數樣本，左對齊成 32 位元。
std::int32_t readLeftJustified(const std::byte* p, int containerBytes) noexcept {
    std::uint32_t word = 0;
    std::memcpy(reinterpret_cast<std::byte*>(&word) + (4 - containerBytes), p,
                static_cast<std::size_t>(containerBytes));
    return static_cast<std::int32_t>(word);
}

/// 浮點樣本量化成 bits 位元，左對齊成 32 位元。
std::int32_t quantize(float value, int bits) noexcept {
    const double scale = std::ldexp(1.0, bits - 1);
    double scaled = std::nearbyint(static_cast<double>(value) * scale);
    if (scaled > scale - 1.0) {
        scaled = scale - 1.0;
    } else if (scaled < -scale) {
        scaled = -scale;
    }
    const auto q = static_cast<std::int64_t>(scaled);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(q) << (32 - bits));
}

} // namespace

QList<AudioFormat> exclusiveCandidates(const AudioFormat& requested) {
    if (requested.isFloat) {
        return {withLayout(requested, 32, 32, true), withLayout(requested, 32, 32, false),
                withLayout(requested, 24, 32, false), withLayout(requested, 24, 24, false),
                withLayout(requested, 16, 16, false)};
    }
    if (requested.bits == 24) {
        return {withLayout(requested, 24, 24, false), withLayout(requested, 24, 32, false)};
    }
    return {requested};
}

WAVEFORMATEXTENSIBLE toWaveFormat(const AudioFormat& format) {
    WAVEFORMATEXTENSIBLE wave{};
    wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave.Format.nChannels = format.channels;
    wave.Format.nSamplesPerSec = format.rate;
    wave.Format.wBitsPerSample = format.container;
    wave.Format.nBlockAlign = static_cast<WORD>(format.channels * format.container / 8);
    wave.Format.nAvgBytesPerSec = format.rate * wave.Format.nBlockAlign;
    wave.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wave.Samples.wValidBitsPerSample = format.bits;
    wave.dwChannelMask = channelMask(format.channels);
    wave.SubFormat = format.isFloat ? kSubtypeFloat : kSubtypePcm;
    return wave;
}

QString describeFormat(const AudioFormat& format) {
    const QString layout = format.isFloat ? QStringLiteral("float%1").arg(format.container)
                           : format.bits == format.container
                               ? QString::number(format.bits)
                               : QStringLiteral("%1-in-%2").arg(format.bits).arg(format.container);
    return QStringLiteral("%1 Hz %2 bit %3ch").arg(format.rate).arg(layout).arg(format.channels);
}

void convertSamples(const std::byte* src, const AudioFormat& from, std::byte* dst,
                    const AudioFormat& to, qint64 frames) noexcept {
    const int fromBytes = from.container / 8;
    const int toBytes = to.container / 8;
    const qint64 samples = frames * from.channels;
    for (qint64 i = 0; i < samples; ++i) {
        const std::byte* in = src + i * fromBytes;
        std::byte* out = dst + i * toBytes;
        if (to.isFloat) {
            float value = 0.0f;
            if (from.isFloat) {
                std::memcpy(&value, in, sizeof value);
            } else {
                value = static_cast<float>(readLeftJustified(in, fromBytes) / 2147483648.0);
            }
            std::memcpy(out, &value, sizeof value);
            continue;
        }
        std::int32_t word = 0;
        if (from.isFloat) {
            float value = 0.0f;
            std::memcpy(&value, in, sizeof value);
            word = quantize(value, to.bits);
        } else {
            word = readLeftJustified(in, fromBytes);
        }
        // 小端：左對齊的 32 位元字取最高的 toBytes 個位元組
        std::memcpy(out, reinterpret_cast<const std::byte*>(&word) + (4 - toBytes),
                    static_cast<std::size_t>(toBytes));
    }
}

OutputErrorKind classifyHresult(long hresult) noexcept {
    switch (static_cast<HRESULT>(hresult)) {
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return OutputErrorKind::FormatNotSupported;
    case AUDCLNT_E_DEVICE_IN_USE:
        return OutputErrorKind::DeviceBusy;
    case AUDCLNT_E_DEVICE_INVALIDATED:
    case AUDCLNT_E_RESOURCES_INVALIDATED:
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
    case kNotFound:
        return OutputErrorKind::DeviceLost;
    default:
        return OutputErrorKind::Other;
    }
}

} // namespace auralis::core
