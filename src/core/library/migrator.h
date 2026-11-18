#pragma once

#include <auralis/result.h>

#include <QString>

#include <vector>

namespace auralis::core {

/// 一個升級腳本：把資料庫從 version - 1 升到 version。
struct Migration {
    int version = 0;
    QString name;
    QString sql;
};

/// 編進程式的升級腳本（:/migrations/NNNN_<描述>.sql），依版本排序。
/// 編號不連續或檔名不合規則時丟 std::logic_error（建置時就該發現的錯誤）。
std::vector<Migration> builtinMigrations();

/// 把 dbPath 升到 migrations 的最新版本，回傳升級後的版本。
/// 已有資料的資料庫升級前先備份成 <dbPath>.bak-v<舊版本>（保留最近 3 份）；
/// 所有腳本在同一個交易裡執行，失敗時還原備份並回傳 Error，不會留下半套 schema。
/// 資料庫版本比程式新時回傳 Error（不降級）。執行緒：任何執行緒，但同一個檔案一次一個。
Result<int> migrateDatabase(const QString& dbPath,
                            const std::vector<Migration>& migrations = builtinMigrations());

/// 只保留 dbPath 最近（版本最大）的 keep 份備份，其餘刪除。
void pruneBackups(const QString& dbPath, int keep);

} // namespace auralis::core
