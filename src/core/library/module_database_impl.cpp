#include "library/module_database_impl.h"

#include "library/sqlite_connection.h"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace auralis::core {

namespace {

int authorizerCallback(void* self, int action, const char* first, const char*, const char*,
                       const char*) {
    return static_cast<ModuleDatabaseImpl*>(self)->authorize(action, first);
}

QString actionName(int action) {
    switch (action) {
    case SQLITE_INSERT:
        return QStringLiteral("寫入");
    case SQLITE_UPDATE:
        return QStringLiteral("更新");
    case SQLITE_DELETE:
        return QStringLiteral("刪除");
    case SQLITE_READ:
        return QStringLiteral("讀取");
    default:
        return QStringLiteral("執行這類敘述（代碼 %1）").arg(action);
    }
}

} // namespace

QSet<QString> ownedTables(const QString& moduleCode) {
    // 歸屬照資料模型；新增模組擁有的表時（核心任務的升級腳本）同時加在這裡
    if (moduleCode == QLatin1String("OUT")) {
        return {QStringLiteral("rg_computed")};
    }
    if (moduleCode == QLatin1String("PL")) {
        return {QStringLiteral("playlists"), QStringLiteral("playlist_items"),
                QStringLiteral("path_remaps")};
    }
    if (moduleCode == QLatin1String("LYR")) {
        return {QStringLiteral("lyrics_cache")};
    }
    return {};
}

ModuleDatabaseImpl::ModuleDatabaseImpl(QString moduleCode, QString dbPath)
    : m_code(std::move(moduleCode)), m_dbPath(std::move(dbPath)), m_tables(ownedTables(m_code)) {}

ModuleDatabaseImpl::~ModuleDatabaseImpl() = default;

int ModuleDatabaseImpl::authorize(int action, const char* table) {
    switch (action) {
    case SQLITE_SELECT:
    case SQLITE_FUNCTION:
    case SQLITE_TRANSACTION:
    case SQLITE_SAVEPOINT:
    case SQLITE_RECURSIVE:
        return SQLITE_OK;
    case SQLITE_INSERT:
    case SQLITE_UPDATE:
    case SQLITE_DELETE:
    case SQLITE_READ:
        if (table != nullptr && m_tables.contains(QString::fromUtf8(table).toLower())) {
            return SQLITE_OK;
        }
        break;
    default:
        break;
    }
    // 回呼在 SQLite 裡面，不能丟例外；先記下來，由呼叫端在 SQLite 返回後丟
    m_denied = QStringLiteral("模組 %1 不能%2 %3")
                   .arg(m_code, actionName(action),
                        table ? QString::fromUtf8(table) : QStringLiteral("（無資料表）"));
    return SQLITE_DENY;
}

Result<SqliteConnection*> ModuleDatabaseImpl::connection() {
    if (m_connection) {
        return m_connection.get();
    }
    if (m_dbPath.isEmpty()) {
        return Error{QStringLiteral("資料庫沒有開啟"), 0, QStringLiteral("ModuleDatabase")};
    }
    auto opened = SqliteConnection::open(m_dbPath);
    if (!opened) {
        return opened.error();
    }
    m_connection = std::move(opened).value();
    sqlite3_set_authorizer(m_connection->handle(), &authorizerCallback, this);
    return m_connection.get();
}

void ModuleDatabaseImpl::throwIfDenied() {
    if (!m_denied.isEmpty()) {
        const QString message = std::exchange(m_denied, QString());
        throw std::logic_error(message.toStdString());
    }
}

Result<qint64> ModuleDatabaseImpl::execute(const QString& sql, const QVariantList& params) {
    const std::lock_guard lock(m_mutex);
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    m_denied.clear();
    auto result = db.value()->execute(sql, params);
    throwIfDenied();
    return result;
}

Result<QList<ModuleDatabase::Row>> ModuleDatabaseImpl::query(const QString& sql,
                                                             const QVariantList& params) {
    const std::lock_guard lock(m_mutex);
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    m_denied.clear();
    auto result = db.value()->query(sql, params);
    throwIfDenied();
    return result;
}

Result<bool> ModuleDatabaseImpl::transaction(const std::function<bool()>& work) {
    const std::lock_guard lock(m_mutex);
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    SqliteConnection& connection = *db.value();
    if (const auto begun = connection.executeScript(QStringLiteral("BEGIN IMMEDIATE;")); !begun) {
        return begun.error();
    }
    bool commit = false;
    try {
        commit = work();
    } catch (...) {
        (void)connection.executeScript(QStringLiteral("ROLLBACK;"));
        throw;
    }
    const auto ended =
        connection.executeScript(commit ? QStringLiteral("COMMIT;") : QStringLiteral("ROLLBACK;"));
    if (!ended) {
        (void)connection.executeScript(QStringLiteral("ROLLBACK;"));
        return ended.error();
    }
    return commit;
}

} // namespace auralis::core
