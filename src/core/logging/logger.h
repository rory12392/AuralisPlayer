#pragma once

#include <auralis/result.h>

#include <QString>

namespace auralis::core {

/// 啟用檔案日誌：<資料夾>\logs\auralis-YYYY-MM-DD.log，同時輸出到 stderr；刪除超過 7 天的日誌。
/// 等級由 AURALIS_LOG_LEVEL（debug／info／warning／error，預設 info）決定。
/// 在 main() 一開始、建立其他執行緒之前呼叫一次。之後各處用 Qt 的 qCInfo／qCWarning 等記錄。
/// 不可以在即時音訊路徑記日誌（會加鎖與 I/O）。
Result<QString> installFileLogger(const QString& dataDir);

/// 程式結束前呼叫：還原預設的訊息處理並關閉檔案。
void uninstallFileLogger();

} // namespace auralis::core
