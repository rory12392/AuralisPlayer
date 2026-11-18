#include "audio/sample_convert.h"

#include <cmath>
#include <cstring>

namespace auralis::core {

void toDouble(const std::byte* src, const AudioFormat& format, std::int64_t frames,
              double* dest) noexcept {
    const std::int64_t count = frames * format.channels;
    for (std::int64_t i = 0; i < count; ++i) {
        if (format.isFloat) {
            float value;
            std::memcpy(&value, src + i * 4, 4);
            dest[i] = value;
        } else if (format.container == 16) {
            std::int16_t value;
            std::memcpy(&value, src + i * 2, 2);
            dest[i] = value / 32768.0;
        } else {
            // 左對齊在 32 位元容器，所以不論有效位元數，除以 2^31 就是滿刻度 ±1.0
            std::int32_t value;
            std::memcpy(&value, src + i * 4, 4);
            dest[i] = value / 2147483648.0;
        }
    }
}

void fromDouble(const double* src, const AudioFormat& format, std::int64_t frames, std::byte* dest,
                TpdfDither& dither) noexcept {
    const std::int64_t count = frames * format.channels;
    const int shift = format.container - format.bits;
    const double scale = std::ldexp(1.0, format.bits - 1);
    const double maxValue = scale - 1.0;
    for (std::int64_t i = 0; i < count; ++i) {
        if (format.isFloat) {
            const float value = static_cast<float>(src[i]);
            std::memcpy(dest + i * 4, &value, 4);
            continue;
        }
        double v = std::floor(src[i] * scale + dither.next() + 0.5);
        v = v < -scale ? -scale : (v > maxValue ? maxValue : v);
        const std::int64_t q = static_cast<std::int64_t>(v) * (std::int64_t{1} << shift);
        if (format.container == 16) {
            const auto value = static_cast<std::int16_t>(q);
            std::memcpy(dest + i * 2, &value, 2);
        } else {
            const auto value = static_cast<std::int32_t>(q);
            std::memcpy(dest + i * 4, &value, 4);
        }
    }
}

} // namespace auralis::core
