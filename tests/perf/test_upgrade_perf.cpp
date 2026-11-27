// 資料庫升級（0002）與背景補值的時間（AC-10-5）：SQL 升級不能讓啟動多等超過 1 秒，補值在背景。
// 預設在測試暫存產生 2,000 首的小音樂庫；正式量測用 AURALIS_PERF_LIBRARY 指到 4 萬首的測試複本
// （同掃描效能測試，只讀、不產生也不刪除任何東西）。
#include "library/library_scanner.h"
#include "library/meta_backfill.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

using namespace auralis::core;
using namespace auralis::test;

namespace {

constexpr qint64 kSqlUpgradeLimitMs = 1000;

QString perfLibrary() {
    const QString given = QString::fromLocal8Bit(qgetenv("AURALIS_PERF_LIBRARY"));
    if (!given.isEmpty() && QFileInfo::exists(given)) {
        return QDir::toNativeSeparators(given);
    }
    const QString root = freshTestDir(QStringLiteral("t-114-perf-library"));
    const QString source = root + QStringLiteral("\\source.flac.tmp");
    writeFlac(source, {{QStringLiteral("TITLE"), QStringLiteral("效能測試曲")},
                       {QStringLiteral("ALBUM"), QStringLiteral("Perf Album")}});
    for (int i = 0; i < 2000; ++i) {
        const QString folder = root + QStringLiteral("\\專輯 %1").arg(i / 50);
        QDir().mkpath(folder);
        REQUIRE(QFile::copy(source, folder + QStringLiteral("\\%1.flac").arg(i)));
    }
    QFile::remove(source);
    return root;
}

std::unique_ptr<SqliteConnection> open(const QString& path) {
    auto db = SqliteConnection::open(path);
    REQUIRE(db.hasValue());
    return std::move(db).value();
}

} // namespace

TEST_CASE("AC-10-5：v1.0 資料庫升級（SQL）在 1 秒內，補值時間另計") {
    const QString root = perfLibrary();
    const QString dir = freshTestDir(QStringLiteral("t-114-perf-db"));
    const QString v1 = dir + QStringLiteral("\\v1.db");

    // 用目前的程式掃描，再退回 v1.0 的 schema（拿掉 0002 的欄位與索引）當作使用者的舊資料庫
    REQUIRE(migrateDatabase(v1).hasValue());
    {
        auto db = open(v1);
        LibraryWriter writer(*db);
        const QList<LibraryRoot> roots{{writer.ensureRoot(root), root}};
        REQUIRE(scanLibrary(*db, roots, {}).added > 0);
        REQUIRE(db->executeScript(QStringLiteral("DROP INDEX tracks_added_at;"
                                                 "ALTER TABLE tracks DROP COLUMN meta_backfill;"
                                                 "ALTER TABLE tracks DROP COLUMN added_at;"
                                                 "ALTER TABLE tracks DROP COLUMN bitrate;"
                                                 "PRAGMA user_version = 1; VACUUM;"))
                    .hasValue());
    }
    const qint64 dbBytes = QFileInfo(v1).size();

    // 只有 SQL（一個交易）
    const QString sqlOnly = dir + QStringLiteral("\\sql-only.db");
    REQUIRE(QFile::copy(v1, sqlOnly));
    qint64 sqlMs = 0;
    {
        auto db = open(sqlOnly);
        QElapsedTimer timer;
        timer.start();
        REQUIRE(db->executeScript(QStringLiteral("BEGIN IMMEDIATE;") + builtinMigrations()[1].sql +
                                  QStringLiteral("\n;\nPRAGMA user_version = 2;\nCOMMIT;"))
                    .hasValue());
        sqlMs = timer.elapsed();
    }

    // 啟動時實際做的：VACUUM INTO 備份＋SQL
    QElapsedTimer timer;
    timer.start();
    REQUIRE(migrateDatabase(v1).hasValue());
    const qint64 upgradeMs = timer.elapsed();

    auto db = open(v1);
    const int tracks = pendingBackfillCount(*db);
    timer.restart();
    const BackfillResult result = runMetaBackfill(*db);
    const qint64 backfillMs = timer.elapsed();
    REQUIRE(result.updated + result.deferred == tracks);
    CHECK(pendingBackfillCount(*db) == result.deferred);

    const QString summary =
        QStringLiteral("升級效能：%1 首（資料庫 %2 MB），SQL 升級 %3 ms，備份＋SQL %4 ms，"
                       "背景補值 %5 ms（補完 %6、留待下次 %7）")
            .arg(tracks)
            .arg(dbBytes / 1048576.0, 0, 'f', 1)
            .arg(sqlMs)
            .arg(upgradeMs)
            .arg(backfillMs)
            .arg(result.updated)
            .arg(result.deferred);
    std::printf("%s\n", summary.toUtf8().constData());
    INFO(summary.toStdString());
    CHECK(sqlMs <= kSqlUpgradeLimitMs);
}
