#pragma once

#include <auralis/result.h>

#include <QString>
#include <QStringList>

namespace auralis::core {

/// 決定資料夾（settings.ini、library.db、快取、日誌）需要的輸入；抽出來讓規則可以單獨測試。
struct DataDirInputs {
    /// 命令列 --data-dir（沒有時為空）。
    QString commandLine;
    /// 環境變數 AURALIS_DATA_DIR（沒有時為空）。
    QString environment;
    /// 是否為開發版（AURALIS_DEV_BUILD）。
    bool devBuild = false;
    /// exe 所在的資料夾；旁邊有 portable.flag 時是免安裝版。
    QString exeDir;
    bool portableFlag = false;
    /// %LOCALAPPDATA%。
    QString localAppData;
};

/// 依序：--data-dir → AURALIS_DATA_DIR → （開發版到這裡就拒絕）→ 免安裝版 <exe>\data →
/// %LOCALAPPDATA%\AuralisPlayer。
/// 開發版拒絕退回到使用者的真實資料夾，避免開發與測試動到真實的設定與音樂庫。
Result<QString> resolveDataDir(const DataDirInputs& inputs);

/// 從目前的程式環境收集輸入。
DataDirInputs currentDataDirInputs(const QString& commandLine);

/// 命令列參數（不含程式本身）裡 --data-dir 的值：「--data-dir <資料夾>」或「--data-dir=<資料夾>」；
/// 沒有時空字串。
QString dataDirArgument(const QStringList& arguments);

/// 建立 QGuiApplication 之前（讀介面縮放時）收集輸入：命令列從 Windows API 取（main 的 argv
/// 是 ANSI 字碼頁，中文路徑會壞），exe 資料夾也一樣。規則和之後的 currentDataDirInputs 相同。
DataDirInputs dataDirInputsBeforeApp();

} // namespace auralis::core
