#pragma once

#include <auralis/audio_format.h>
#include <auralis/audio_output.h>

#include <QList>
#include <QString>

#include <windows.h>

#include <mmreg.h>

#include <cstddef>

namespace auralis::core {

/// 獨占模式依序嘗試的裝置格式（取樣率、聲道與來源相同，不重取樣，AC-02-2）：
/// 整數 16 → 16；24（左對齊在 32 位元容器）→ 24-in-24、24-in-32；32 → 32；
/// 浮點 → 32 位元浮點，裝置不收浮點時依序 32、24-in-32、24-in-24、16 位元整數（有損來源本來
/// 就不是位元完美）。回傳的 container 可以是 24，表示每個樣本 3 個位元組。
QList<AudioFormat> exclusiveCandidates(const AudioFormat& requested);

/// 裝置格式 → WAVEFORMATEXTENSIBLE（container 是 wBitsPerSample、bits 是 wValidBitsPerSample）。
WAVEFORMATEXTENSIBLE toWaveFormat(const AudioFormat& format);

/// 給日誌與錯誤訊息看的格式，例：44100 Hz 24-in-32。
QString describeFormat(const AudioFormat& format);

/// 把 frames 個樣本框從 from 的排列（音源交出的）轉成 to 的排列（裝置要的）。
/// 整數 → 整數只搬位元（24-in-32 → 24-in-24 取每個樣本的高 3 位元組，位元完美）；
/// 浮點 → 整數四捨五入並限制在範圍內（不抖動）。即時路徑：不配置、不丟例外。
void convertSamples(const std::byte* src, const AudioFormat& from, std::byte* dst,
                    const AudioFormat& to, qint64 frames) noexcept;

/// WASAPI 的 HRESULT → 輸出失敗的種類（AC-02-2、AC-02-4）。
OutputErrorKind classifyHresult(long hresult) noexcept;

} // namespace auralis::core
