// ASIO 原生 DSD 的驅動呼叫與樣本搬運（US-04 AC-4；只在 AURALIS_WITH_ASIO 時編譯）。
#pragma once

#include <QtGlobal>

#include <cstddef>

struct IASIO;

namespace auralis::mod::out {

/// 用 ASIOFuture(kAsioSetIoFormat) 把驅動切到 DSD（dsd = true）或切回 PCM。
/// 能不能原生 DSD 只看這個呼叫的結果：回 ASE_SUCCESS 才算成功（不信 kAsioCanDoIoFormat）。
/// 控制執行緒呼叫。
bool setAsioIoFormat(IASIO* driver, bool dsd) noexcept;

/// 把 dsdFrames 個 DSD 樣本框（核心的 DSD 約定：每聲道 4 個位元組、聲道交錯、最高位在前）
/// 寫進 outputCount 個 ASIO DSDInt8MSB1 聲道緩衝（每個位元組 8 個 DSD 位元、最高位在前，
/// 依時間排列）。只搬位元組、不改值；單聲道來源送到每一個輸出聲道。
/// 即時路徑：不配置、不加鎖、不 I/O。
void deinterleaveDsdToAsio(const std::byte* interleaved, int channels, qint64 dsdFrames,
                           void* const* outputs, int outputCount) noexcept;

} // namespace auralis::mod::out
