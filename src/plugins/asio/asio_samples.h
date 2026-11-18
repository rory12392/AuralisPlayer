// ASIO 的樣本搬運：把核心交出的交錯樣本拆成每個聲道一個緩衝，並轉成驅動的整數格式。
#pragma once

#include <auralis/audio_format.h>

#include <cstddef>

namespace auralis::mod::out {

/// 把 frames 個交錯樣本框（format 的排列：整數左對齊在 16 或 32 位元容器，或 32 位元浮點）
/// 寫進 outputCount 個聲道緩衝，每個樣本 deviceBytes 位元組（2、3、4：ASIO 的 Int16LSB、
/// Int24LSB、Int32LSB，小端序）。整數來源只搬位元、不改值（位元完美，AC-03-2）：
/// 驅動的位元數比來源多時低位補 0。單聲道來源送到每一個輸出聲道。
/// 即時路徑：不配置、不加鎖、不 I/O。
void deinterleaveToAsio(const std::byte* interleaved, const AudioFormat& format, qint64 frames,
                        void* const* outputs, int outputCount, int deviceBytes) noexcept;

} // namespace auralis::mod::out
