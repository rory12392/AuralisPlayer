#pragma once

#include <auralis/result.h>

#include <QList>
#include <QString>
#include <QVariant>

#include <memory>

struct sqlite3;

namespace auralis::core {

/// 一條 SQLite 連線（WAL、外鍵開啟、忙碌時等 5 秒）。核心內部使用；模組經 ModuleDatabase。
/// 連線以序列化模式開啟，可以跨執行緒呼叫，但交易要由呼叫端自己保證不被別的執行緒插入。
class SqliteConnection {
public:
    using Row = QVariantList;

    /// 開啟或建立資料庫檔。path 可以含任何 Unicode 字元。
    static Result<std::unique_ptr<SqliteConnection>> open(const QString& path);
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    /// 執行多句、沒有參數的 SQL（升級腳本）。回傳最後一句的受影響列數。
    Result<qint64> executeScript(const QString& sql);
    /// 執行一句、綁定參數；回傳受影響的列數。多於一句時回傳 Error。
    Result<qint64> execute(const QString& sql, const QVariantList& params = {});
    /// 執行一句查詢並取回所有列。
    Result<QList<Row>> query(const QString& sql, const QVariantList& params = {});

    /// 給需要 SQLite C API 的核心程式（例：設定 authorizer）。
    sqlite3* handle() const noexcept { return m_db; }

private:
    explicit SqliteConnection(sqlite3* db) noexcept : m_db(db) {}
    Result<qint64> run(const QString& sql, const QVariantList& params, QList<Row>* rows);
    Error lastError(const QString& call) const;

    sqlite3* m_db = nullptr;
};

} // namespace auralis::core
