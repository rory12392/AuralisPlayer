#pragma once

#include <auralis/services.h>

#include <QString>
#include <QtGlobal>

namespace auralis::mod::out {

/// DSD 輸出方式（AC-04-2），記在 settings.ini 的 OUT/dsdMode（"dop"、"native"、"pcm"）。
enum class DsdMode { Dop, Native, Pcm };

/// 輸出路徑的種類：由輸出後端工廠的名稱判斷（「ASIO」「WASAPI 共享」，其他當成 WASAPI 獨占）。
enum class OutputKind { Exclusive, Shared, Asio };

/// 設定值：OUT/dsdMode 的預設是 DoP（T-30 結論），OUT/dsdPcmRate 的預設是 176400。
QString dsdModeKey(DsdMode mode);
DsdMode dsdModeFromKey(const QString& key);
/// 轉 PCM 可選的取樣率（88.2／176.4／352.8 kHz，對 DSD64–256 都是整數倍）；其他值回預設。
quint32 validPcmRate(qint64 rate);

OutputKind outputKind(const QString& backendName);

/// 這個方式在這個輸出路徑下能不能用：不能用時回傳寫在選項上的原因
/// （例：「需選 ASIO」），能用時空字串。
/// ASIO 原生 DSD 只有 ASIO 收得下；DoP 經 WASAPI 共享會被混音器改成噪音，所以不提供。
/// nativeDsd 是有沒有能收原生 DSD 的輸出外掛（T-103）：沒有時原生 DSD 一律不能用（「需要 ASIO
/// 外掛」）。
QString unavailableReason(DsdMode mode, OutputKind kind, bool nativeDsd = true);

/// 實際要用的方式：選的方式能用就用它；ASIO 原生 DSD 在 ASIO 以外改用 DoP（共享模式改轉 PCM），
/// DoP 在共享模式改轉 PCM。使用者選的方式照樣記住，換回能用的輸出時恢復。
DsdMode effectiveMode(DsdMode chosen, OutputKind kind, bool nativeDsd = true);

/// 輸出狀態上的 DSD 輸出方式（「DoP」「原生」「轉 PCM」）；不是 DSD 曲目時空字串。
/// 判斷：裝置格式是 DSD 是原生；原因有「DSD 轉 PCM」是轉 PCM；其他（裝置格式是 PCM）是 DoP。
QString dsdRouteLabel(const OutputStatus& status);

/// DSD 曲目因為裝置不支援這個格式被跳過時，建議改用的方式（AC-04-2、規格 §1 情況 3）。
QString unsupportedSuggestion(DsdMode effective);

/// 路徑是不是 DSD 檔（.dsf、.dff，不分大小寫）。
bool isDsdPath(const QString& path);

} // namespace auralis::mod::out
