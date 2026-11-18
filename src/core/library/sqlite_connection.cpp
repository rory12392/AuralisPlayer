#include "library/sqlite_connection.h"

#include <QElapsedTimer>
#include <QThread>

#include <sqlite3.h>

namespace auralis::core {

namespace {

constexpr int kBusyTimeoutMs = 5000;
constexpr int kRetryIntervalMs = 20;

/// 敘述的 RAII 包裝，確保任何路徑都會 finalize。
struct Statement {
    sqlite3_stmt* stmt = nullptr;
    ~Statement() { sqlite3_finalize(stmt); }
};

bool bindValue(sqlite3_stmt* stmt, int index, const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    }
    switch (value.typeId()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        return sqlite3_bind_int64(stmt, index, value.toLongLong()) == SQLITE_OK;
    case QMetaType::Double:
    case QMetaType::Float:
        return sqlite3_bind_double(stmt, index, value.toDouble()) == SQLITE_OK;
    case QMetaType::QByteArray: {
        const QByteArray bytes = value.toByteArray();
        return sqlite3_bind_blob64(stmt, index, bytes.constData(),
                                   static_cast<sqlite3_uint64>(bytes.size()),
                                   SQLITE_TRANSIENT) == SQLITE_OK;
    }
    default: {
        const QString text = value.toString();
        return sqlite3_bind_text16(stmt, index, text.utf16(),
                                   static_cast<int>(text.size() * sizeof(char16_t)),
                                   SQLITE_TRANSIENT) == SQLITE_OK;
    }
    }
}

QVariant columnValue(sqlite3_stmt* stmt, int column) {
    switch (sqlite3_column_type(stmt, column)) {
    case SQLITE_INTEGER:
        return QVariant::fromValue<qint64>(sqlite3_column_int64(stmt, column));
    case SQLITE_FLOAT:
        return sqlite3_column_double(stmt, column);
    case SQLITE_TEXT: {
        const auto* text = static_cast<const char16_t*>(sqlite3_column_text16(stmt, column));
        const int bytes = sqlite3_column_bytes16(stmt, column);
        return QString::fromUtf16(text, bytes / static_cast<int>(sizeof(char16_t)));
    }
    case SQLITE_BLOB: {
        const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt, column));
        return QByteArray(data, sqlite3_column_bytes(stmt, column));
    }
    default:
        return {};
    }
}

} // namespace

Result<std::unique_ptr<SqliteConnection>> SqliteConnection::open(const QString& path) {
    sqlite3* db = nullptr;
    // sqlite3_open_v2 的路徑是 UTF-8；Windows 的 VFS 會轉成寬字元，所以任何 Unicode 路徑都可以
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &db, flags, nullptr);
    std::unique_ptr<SqliteConnection> connection(new SqliteConnection(db));
    if (rc != SQLITE_OK) {
        return connection->lastError(QStringLiteral("sqlite3_open_v2"));
    }
    sqlite3_extended_result_codes(db, 1);
    sqlite3_busy_timeout(db, kBusyTimeoutMs);
    // 新資料庫第一次切到 WAL 時，另一條連線（例：同時啟動的另一個程式）正在切換的話
    // SQLite 不經忙碌等待直接回報 locked，所以自己重試到忙碌等待的時限
    QElapsedTimer waited;
    waited.start();
    for (;;) {
        const auto pragmas = connection->executeScript(
            QStringLiteral("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; "
                           "PRAGMA synchronous=NORMAL;"));
        if (pragmas) {
            return connection;
        }
        const bool busy = (pragmas.error().hresult & 0xff) == SQLITE_BUSY ||
                          (pragmas.error().hresult & 0xff) == SQLITE_LOCKED;
        if (!busy || waited.elapsed() > kBusyTimeoutMs) {
            return pragmas.error();
        }
        QThread::msleep(kRetryIntervalMs);
    }
}

SqliteConnection::~SqliteConnection() {
    // 還有未 finalize 的敘述時 sqlite3_close_v2 會延後關閉，不會失敗
    sqlite3_close_v2(m_db);
}

Result<qint64> SqliteConnection::executeScript(const QString& sql) {
    char* message = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        // hresult 欄放 SQLite 的結果碼，讓呼叫端分得出忙碌與其他錯誤
        Error error{QString::fromUtf8(message ? message : sqlite3_errstr(rc)), rc,
                    QStringLiteral("sqlite3_exec")};
        sqlite3_free(message);
        return error;
    }
    return static_cast<qint64>(sqlite3_changes64(m_db));
}

Result<qint64> SqliteConnection::execute(const QString& sql, const QVariantList& params) {
    return run(sql, params, nullptr);
}

Result<QList<SqliteConnection::Row>> SqliteConnection::query(const QString& sql,
                                                             const QVariantList& params) {
    QList<Row> rows;
    const auto result = run(sql, params, &rows);
    if (!result) {
        return result.error();
    }
    return rows;
}

Result<qint64> SqliteConnection::run(const QString& sql, const QVariantList& params,
                                     QList<Row>* rows) {
    Statement statement;
    const void* tail = nullptr;
    const auto bytes = static_cast<int>(sql.size() * sizeof(char16_t));
    if (sqlite3_prepare16_v3(m_db, sql.utf16(), bytes, 0, &statement.stmt, &tail) != SQLITE_OK) {
        return lastError(QStringLiteral("sqlite3_prepare16_v3"));
    }
    const auto consumed =
        static_cast<const char16_t*>(tail) - reinterpret_cast<const char16_t*>(sql.utf16());
    if (!sql.mid(consumed).trimmed().isEmpty()) {
        return Error{QStringLiteral("一次只能執行一句 SQL"), 0, QStringLiteral("execute")};
    }
    if (statement.stmt == nullptr) {
        return qint64{0}; // 只有空白或註解
    }
    if (sqlite3_bind_parameter_count(statement.stmt) != params.size()) {
        return Error{QStringLiteral("參數數量不符：SQL 需要 %1 個，給了 %2 個")
                         .arg(sqlite3_bind_parameter_count(statement.stmt))
                         .arg(params.size()),
                     0, QStringLiteral("sqlite3_bind")};
    }
    for (int i = 0; i < params.size(); ++i) {
        if (!bindValue(statement.stmt, i + 1, params[i])) {
            return lastError(QStringLiteral("sqlite3_bind"));
        }
    }
    const int columns = sqlite3_column_count(statement.stmt);
    for (;;) {
        const int rc = sqlite3_step(statement.stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            return lastError(QStringLiteral("sqlite3_step"));
        }
        if (rows != nullptr) {
            Row row;
            row.reserve(columns);
            for (int c = 0; c < columns; ++c) {
                row.append(columnValue(statement.stmt, c));
            }
            rows->append(std::move(row));
        }
    }
    return static_cast<qint64>(sqlite3_changes64(m_db));
}

Error SqliteConnection::lastError(const QString& call) const {
    return Error{QString::fromUtf8(sqlite3_errmsg(m_db)), 0, call};
}

} // namespace auralis::core
