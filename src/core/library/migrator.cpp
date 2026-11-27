#include "library/migrator.h"

#include "library/sqlite_connection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <sqlite3.h>

#include <algorithm>
#include <map>
#include <stdexcept>

Q_LOGGING_CATEGORY(lcMigrations, "auralis.library")

// 核心是靜態函式庫，資源要明確初始化；Q_INIT_RESOURCE 只能在全域命名空間呼叫
static void initMigrationResources() {
    Q_INIT_RESOURCE(migrations);
}

namespace auralis::core {

namespace {

constexpr int kBackupsToKeep = 3;
// 等另一個程式升級完的上限（一般查詢只等 5 秒）
constexpr int kUpgradeWaitMs = 60000;

QString backupPath(const QString& dbPath, int version) {
    return dbPath + QStringLiteral(".bak-v%1").arg(version);
}

void removeDatabaseFiles(const QString& dbPath) {
    for (const char* suffix : {"", "-wal", "-shm"}) {
        QFile::remove(dbPath + QLatin1String(suffix));
    }
}

Result<int> userVersion(SqliteConnection& db) {
    const auto rows = db.query(QStringLiteral("PRAGMA user_version"));
    if (!rows) {
        return rows.error();
    }
    return static_cast<int>(rows.value().at(0).at(0).toLongLong());
}

Error withContext(Error error, const QString& context) {
    error.message = context + error.message;
    return error;
}

Result<qint64> backupDatabase(const QString& dbPath, const QString& backup) {
    auto reader = SqliteConnection::open(dbPath);
    if (!reader) {
        return reader.error();
    }
    QFile::remove(backup);
    return reader.value()->execute(QStringLiteral("VACUUM INTO ?"), {backup});
}

} // namespace

std::vector<Migration> builtinMigrations() {
    initMigrationResources();
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})_([a-z0-9_]+)\\.sql$"));
    std::vector<Migration> migrations;
    const QDir dir(QStringLiteral(":/migrations"));
    for (const QString& name : dir.entryList({QStringLiteral("*.sql")}, QDir::Files)) {
        const auto match = pattern.match(name);
        if (!match.hasMatch()) {
            throw std::logic_error("升級腳本的檔名要是 NNNN_<描述>.sql：" + name.toStdString());
        }
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::logic_error("讀不到內嵌的升級腳本：" + name.toStdString());
        }
        migrations.push_back({match.captured(1).toInt(), name, QString::fromUtf8(file.readAll())});
    }
    std::sort(migrations.begin(), migrations.end(),
              [](const Migration& a, const Migration& b) { return a.version < b.version; });
    for (size_t i = 0; i < migrations.size(); ++i) {
        if (migrations[i].version != static_cast<int>(i) + 1) {
            throw std::logic_error("升級腳本的編號要從 0001 起連續：" +
                                   migrations[i].name.toStdString());
        }
    }
    return migrations;
}

Result<int> migrateDatabase(const QString& dbPath, const std::vector<Migration>& migrations) {
    const int latest = migrations.empty() ? 0 : migrations.back().version;
    auto opened = SqliteConnection::open(dbPath);
    if (!opened) {
        return withContext(opened.error(), QStringLiteral("無法開啟資料庫：%1：").arg(dbPath));
    }
    auto db = std::move(opened).value();
    const auto current = userVersion(*db);
    if (!current) {
        return current.error();
    }
    const int from = current.value();
    if (from == latest) {
        return latest;
    }
    if (from > latest) {
        return Error{QStringLiteral("資料庫版本 v%1 比這個程式支援的 v%2 新，請改用新版程式")
                         .arg(from)
                         .arg(latest),
                     0, QStringLiteral("migrateDatabase")};
    }

    // 先取得寫入鎖再確認一次版本、再備份：同時啟動兩個程式時，後拿到鎖的那個等先開的升級完，
    // 看到的已經是新版本，不會重複備份（也不會把升級後的內容寫進舊版本的備份）。
    // 先開的程式升級可能很久（大音樂庫、硬碟忙碌），所以這裡的忙碌等待比一般查詢長
    sqlite3_busy_timeout(db->handle(), kUpgradeWaitMs);
    if (const auto begun = db->executeScript(QStringLiteral("BEGIN IMMEDIATE;")); !begun) {
        return withContext(begun.error(),
                           QStringLiteral("無法取得資料庫的寫入鎖（另一個程式在 %1 秒內沒有"
                                          "完成升級或寫入）：")
                               .arg(kUpgradeWaitMs / 1000));
    }
    const auto locked = userVersion(*db);
    if (!locked || locked.value() != from) {
        (void)db->executeScript(QStringLiteral("ROLLBACK;"));
        if (locked && locked.value() == latest) {
            return latest;
        }
        return Error{QStringLiteral("資料庫在升級時被其他程式改變了版本"), 0,
                     QStringLiteral("migrateDatabase")};
    }

    // 版本 0 是剛建立的空資料庫，沒有東西要備份
    const bool backedUp = from > 0;
    if (backedUp) {
        // VACUUM INTO 不能在交易裡執行，改用另一條連線讀；寫入鎖在這條連線上，
        // 讀到的就是升級前、不會再被別人改的內容（WAL 裡還沒寫回的也包含在內）
        const auto copied = backupDatabase(dbPath, backupPath(dbPath, from));
        if (!copied) {
            (void)db->executeScript(QStringLiteral("ROLLBACK;"));
            return withContext(copied.error(), QStringLiteral("升級前備份失敗："));
        }
        pruneBackups(dbPath, kBackupsToKeep);
    }

    QString script;
    for (const auto& migration : migrations) {
        if (migration.version > from) {
            script += migration.sql + QStringLiteral("\n;\n");
        }
    }
    script += QStringLiteral("PRAGMA user_version = %1;\nCOMMIT;\n").arg(latest);
    const auto applied = db->executeScript(script);
    if (applied) {
        qCInfo(lcMigrations).noquote() << "資料庫已升級：v" << from << "→ v" << latest;
        return latest;
    }

    // 交易已經讓資料庫回到原狀；仍照規則還原備份，連線關掉後才能覆蓋檔案
    (void)db->executeScript(QStringLiteral("ROLLBACK;"));
    db.reset();
    QString restored = QStringLiteral("交易已還原");
    if (backedUp) {
        removeDatabaseFiles(dbPath);
        restored = QFile::copy(backupPath(dbPath, from), dbPath)
                       ? QStringLiteral("已還原備份 %1").arg(backupPath(dbPath, from))
                       : QStringLiteral("還原備份失敗，備份在 %1").arg(backupPath(dbPath, from));
    }
    auto error = applied.error();
    error.message = QStringLiteral("資料庫升級失敗（v%1 → v%2）：%3；%4")
                        .arg(from)
                        .arg(latest)
                        .arg(error.message, restored);
    qCCritical(lcMigrations).noquote() << error.message;
    return error;
}

void pruneBackups(const QString& dbPath, int keep) {
    const QFileInfo info(dbPath);
    const QString prefix = info.fileName() + QStringLiteral(".bak-v");
    std::map<int, QString> backups; // 版本 → 檔案，由小到大
    const QDir dir = info.absoluteDir();
    for (const QString& name : dir.entryList({prefix + QLatin1Char('*')}, QDir::Files)) {
        bool ok = false;
        const int version = name.mid(prefix.size()).toInt(&ok);
        if (ok) {
            backups.emplace(version, dir.filePath(name));
        }
    }
    while (static_cast<int>(backups.size()) > keep) {
        QFile::remove(backups.begin()->second);
        backups.erase(backups.begin());
    }
}

} // namespace auralis::core
