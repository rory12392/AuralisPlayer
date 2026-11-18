#pragma once

#include <auralis/audio_format.h>

#include <cstddef>
#include <cstdint>

namespace auralis::core {

/// 三角機率分布（TPDF）抖動：兩個均勻亂數相減，範圍 (-1, 1) 個最低位元。
/// 用 xorshift，不配置記憶體；每個引擎一份，只在解碼執行緒使用。
class TpdfDither {
public:
    explicit TpdfDither(std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : m_state(seed) {}
    double next() noexcept { return uniform() - uniform(); }

private:
    double uniform() noexcept {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 7;
        m_state ^= m_state << 17;
        return static_cast<double>(m_state >> 11) * (1.0 / 9007199254740992.0);
    }

    std::uint64_t m_state;
};

/// 把 frames 個樣本框（format 的排列）轉成交錯的 64 位元浮點，滿刻度 ±1.0。不配置記憶體。
void toDouble(const std::byte* src, const AudioFormat& format, std::int64_t frames,
              double* dest) noexcept;

/// 把 64 位元浮點轉回 format 的排列。整數格式在有效位元數加 TPDF 抖動再四捨五入、限幅，
/// 左對齊放回容器；浮點格式直接轉 32 位元浮點。不配置記憶體。
void fromDouble(const double* src, const AudioFormat& format, std::int64_t frames, std::byte* dest,
                TpdfDither& dither) noexcept;

} // namespace auralis::core
