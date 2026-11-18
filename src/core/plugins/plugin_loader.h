#pragma once

#include <auralis/plugin_entry.h>
#include <auralis/result.h>

#include <QByteArray>
#include <QString>

namespace auralis::core {

/// 檔案的 SHA-256（小寫十六進位）。
Result<QByteArray> sha256OfFile(const QString& path);

/// 載入一個外掛 DLL 的結果。
struct PluginLoadResult {
    enum class Kind {
        Loaded,       ///< 已載入，entry 有效（DLL 到程式結束都不卸載）
        HashMismatch, ///< 雜湊和官方的不同（沒有載入）
        AbiMismatch,  ///< 進入點回傳的 ABI 版本不同（已卸載）
        Failed,       ///< 載入、找進入點或進入點失敗（已卸載）
    };
    Kind kind = Kind::Failed;
    /// 失敗的原因（寫給使用者看）。
    QString reason;
    const AuralisPluginEntry* entry = nullptr;
};

/// 驗證並載入外掛 DLL：先用不讓其他程式寫入的方式開檔、算 SHA-256 和 expectedSha256 比對，
/// 檔案保持開著（比對後到載入前不能被換掉），符合才用 LoadLibraryExW 載入。
/// 只用絕對路徑，相依的 DLL 只從外掛自己的資料夾與 System32 找（不找目前目錄與 PATH）；
/// 主程式已載入的 Qt 與 C++ 執行階段會直接沿用。
/// 進入點丟例外（含結構化例外）或回傳空值時卸載並回 Failed。只在 UI 執行緒、模組啟動前呼叫。
PluginLoadResult loadPluginDll(const QString& dllPath, const QByteArray& expectedSha256);

} // namespace auralis::core
