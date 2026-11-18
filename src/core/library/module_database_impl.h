#pragma once

#include <auralis/module_database.h>

#include <QSet>
#include <QString>

#include <memory>
#include <mutex>

namespace auralis::core {

class SqliteConnection;

/// 模組擁有的資料表（資料模型的「模組擁有」表）。沒有列出的模組沒有可寫的表。
QSet<QString> ownedTables(const QString& moduleCode);

/// ModuleDatabase 的實作：每個模組一條自己的連線，用 SQLite 的 authorizer 擋下
/// 白名單外的讀寫與 DDL，被擋下時丟 std::logic_error。第一次使用時才開連線。
class ModuleDatabaseImpl final : public ModuleDatabase {
public:
    /// dbPath 為空時（例：資料庫沒有開成功）每個呼叫都回傳 Error。
    ModuleDatabaseImpl(QString moduleCode, QString dbPath);
    ~ModuleDatabaseImpl() override;

    // 預設參數和介面相同（覆寫不會繼承預設參數）
    Result<qint64> execute(const QString& sql, const QVariantList& params = {}) override;
    Result<QList<Row>> query(const QString& sql, const QVariantList& params = {}) override;
    Result<bool> transaction(const std::function<bool()>& work) override;

    /// 給 authorizer 回呼用；不是公開介面。
    int authorize(int action, const char* table);

private:
    Result<SqliteConnection*> connection();
    /// 準備或執行失敗後，如果是白名單擋下的就丟 logic_error。
    void throwIfDenied();

    QString m_code;
    QString m_dbPath;
    QSet<QString> m_tables;
    std::recursive_mutex m_mutex;
    std::unique_ptr<SqliteConnection> m_connection;
    QString m_denied;
};

} // namespace auralis::core
