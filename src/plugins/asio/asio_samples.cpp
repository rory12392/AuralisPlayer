#include "asio_samples.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace auralis::mod::out {

namespace {

/// 一個樣本轉成左對齊的 32 位元整數。
std::int32_t leftAligned(const std::byte* sample, const AudioFormat& format) noexcept {
    if (format.isFloat) {
        float value = 0.0f;
        std::memcpy(&value, sample, sizeof value);
        // 浮點來源（有損格式、32 位元浮點 WAV）本來就不是位元完美的對象，四捨五入並截在範圍內
        const double scaled =
            std::nearbyint(std::clamp(static_cast<double>(value), -1.0, 1.0) * 2147483648.0);
        return static_cast<std::int32_t>(std::clamp(scaled, -2147483648.0, 2147483647.0));
    }
    if (format.container == 16) {
        std::int16_t value = 0;
        std::memcpy(&value, sample, sizeof value);
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(value)) << 16);
    }
    std::int32_t value = 0;
    std::memcpy(&value, sample, sizeof value);
    return value;
}

} // namespace

void deinterleaveToAsio(const std::byte* interleaved, const AudioFormat& format, qint64 frames,
                        void* const* outputs, int outputCount, int deviceBytes) noexcept {
    const int sampleBytes = format.container / 8;
    const int frameBytes = format.bytesPerFrame();
    for (int channel = 0; channel < outputCount; ++channel) {
        const int source = format.channels == 1 ? 0 : channel;
        auto* out = static_cast<std::uint8_t*>(outputs[channel]);
        const std::byte* in = interleaved + source * sampleBytes;
        for (qint64 f = 0; f < frames; ++f, in += frameBytes, out += deviceBytes) {
            const auto value = static_cast<std::uint32_t>(leftAligned(in, format));
            // 小端序取最高的 deviceBytes 個位元組
            for (int b = 0; b < deviceBytes; ++b) {
                out[b] = static_cast<std::uint8_t>(value >> (8 * (4 - deviceBytes + b)));
            }
        }
    }
}

} // namespace auralis::mod::out
