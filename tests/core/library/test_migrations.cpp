// 資料庫升級：空資料庫升到最新、每個舊版本升到最新、失敗時還原備份、備份只留 3 份。
#include "library/migrator.h"
#include "library/sqlite_connection.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <atomic>
#include <thread>

using auralis::core::builtinMigrations;
using auralis::core::migrateDatabase;
using auralis::core::Migration;
using auralis::core::pruneBackups;
using auralis::core::SqliteConnection;

namespace {

QString testRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

std::unique_ptr<SqliteConnection> openDb(const QString& path) {
    auto db = SqliteConnection::open(path);
    REQUIRE(db.hasValue());
    return std::move(db).value();
}

QVariant scalar(SqliteConnection& db, const QString& sql, const QVariantList& params = {}) {
    const auto rows = db.query(sql, params);
    REQUIRE(rows.hasValue());
    REQUIRE(rows.value().size() == 1);
    return rows.value().first().first();
}

/// sqlite_schema 裡的資料表、索引、觸發器（排除 SQLite 自己的與 FTS 的影子表）
QStringList schemaObjects(const QString& path) {
    auto db = openDb(path);
    const auto rows = db->query(
        QStringLiteral("SELECT type || ':' || name || ':' || ifnull(sql, '') FROM sqlite_schema "
                       "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"));
    REQUIRE(rows.hasValue());
    QStringList objects;
    for (const auto& row : rows.value()) {
        objects << row.first().toString();
    }
    return objects;
}

} // namespace

TEST_CASE("資料庫：升級腳本從 0001 起連續，且都編進程式") {
    const auto migrations = builtinMigrations();
    REQUIRE_FALSE(migrations.empty());
    CHECK(migrations.front().name == QStringLiteral("0001_initial.sql"));
    for (size_t i = 0; i < migrations.size(); ++i) {
        CHECK(migrations[i].version == static_cast<int>(i) + 1);
        CHECK_FALSE(migrations[i].sql.trimmed().isEmpty());
    }
}

TEST_CASE("資料庫：空資料庫升到最新，建立資料模型的全部資料表並開啟 WAL") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    // 路徑含中日文與全形字元，確認 SQLite 開得了 Unicode 路徑
    const QString path = dir.filePath(QStringLiteral("音楽ライブラリ／ＮieR/library.db"));
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));

    const auto latest = static_cast<int>(builtinMigrations().size());
    const auto result = migrateDatabase(path);
    REQUIRE(result.hasValue());
    CHECK(result.value() == latest);

    auto db = openDb(path);
    CHECK(scalar(*db, QStringLiteral("PRAGMA user_version")).toInt() == latest);
    CHECK(scalar(*db, QStringLiteral("PRAGMA journal_mode")).toString() == QStringLiteral("wal"));
    const QStringList tables = {"library_roots", "folders",   "files",          "tracks",
                                "tracks_fts",    "albums",    "covers",         "scan_reports",
                                "rg_computed",   "playlists", "playlist_items", "path_remaps",
                                "lyrics_cache"};
    for (const QString& table : tables) {
        INFO(table.toStdString());
        CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE name = ?"),
                     {table})
                  .toInt() == 1);
    }
    // 第二次執行不做任何事、也不產生備份
    CHECK(migrateDatabase(path).value() == latest);
    CHECK_FALSE(QFile::exists(path + QStringLiteral(".bak-v%1").arg(latest)));
}

TEST_CASE("資料庫：每個舊版本都能升到最新，結果和從空資料庫升級相同") {
    const auto migrations = builtinMigrations();
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString fresh = dir.filePath(QStringLiteral("fresh.db"));
    REQUIRE(migrateDatabase(fresh).hasValue());
    const QStringList expected = schemaObjects(fresh);

    for (size_t start = 1; start < migrations.size(); ++start) {
        INFO("從 v" << start << " 升級");
        const QString path = dir.filePath(QStringLiteral("from-v%1.db").arg(start));
        const std::vector<Migration> prefix(migrations.begin(),
                                            migrations.begin() + static_cast<long>(start));
        REQUIRE(migrateDatabase(path, prefix).value() == static_cast<int>(start));
        REQUIRE(migrateDatabase(path).value() == static_cast<int>(migrations.size()));
        CHECK(schemaObjects(path) == expected);
        CHECK(QFile::exists(path + QStringLiteral(".bak-v%1").arg(start)));
    }
}

TEST_CASE("資料庫：升級失敗時還原備份，不留下半套 schema") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    auto migrations = builtinMigrations();
    const int current = static_cast<int>(migrations.size());
    REQUIRE(migrateDatabase(path).value() == current);
    {
        auto db = openDb(path);
        REQUIRE(db->execute(QStringLiteral("INSERT INTO library_roots(path, path_key, added_at) "
                                           "VALUES ('D:\\音樂', 'd:\\音樂', 1)"))
                    .hasValue());
    }

    // 故障注入：下一版的腳本先建一張表，再寫入不存在的表而失敗
    migrations.push_back({current + 1, QStringLiteral("9999_broken.sql"),
                          QStringLiteral("CREATE TABLE half_done(a);\n"
                                         "INSERT INTO no_such_table VALUES (1);")});
    const auto result = migrateDatabase(path, migrations);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("已還原備份")));

    auto db = openDb(path);
    CHECK(scalar(*db, QStringLiteral("PRAGMA user_version")).toInt() == current);
    CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM library_roots")).toInt() == 1);
    CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE name = 'half_done'"))
              .toInt() == 0);
    CHECK(QFile::exists(path + QStringLiteral(".bak-v%1").arg(current)));
}

TEST_CASE("資料庫：同時啟動的程式一起升級同一個新資料庫，都成功") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    const auto migrations = builtinMigrations();
    std::vector<std::thread> threads;
    std::vector<QString> errors(4);
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i] {
            const auto result = migrateDatabase(path, migrations);
            if (!result) {
                errors[i] = result.error().call + QStringLiteral("：") + result.error().message;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const QString& error : errors) {
        CHECK(error.toStdString() == "");
    }
}

TEST_CASE("資料庫：版本比程式新時拒絕開啟，不降級") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    {
        auto db = openDb(path);
        REQUIRE(db->executeScript(QStringLiteral("PRAGMA user_version = 999;")).hasValue());
    }
    const auto result = migrateDatabase(path);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("v999")));
}

TEST_CASE("資料庫：升級前的備份只保留最近 3 份") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    for (int version : {1, 2, 3, 4, 10}) {
        QFile file(path + QStringLiteral(".bak-v%1").arg(version));
        REQUIRE(file.open(QIODevice::WriteOnly));
    }
    pruneBackups(path, 3);
    const QStringList left =
        QDir(dir.path()).entryList({QStringLiteral("library.db.bak-v*")}, QDir::Files, QDir::Name);
    CHECK(left == QStringList{"library.db.bak-v10", "library.db.bak-v3", "library.db.bak-v4"});
}

TEST_CASE("資料庫：tracks 的搜尋文字由觸發器同步到 FTS5 trigram 索引") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    REQUIRE(migrateDatabase(path).hasValue());
    auto db = openDb(path);
    REQUIRE(db->executeScript(
                  QStringLiteral("INSERT INTO library_roots VALUES (1, 'D:\\m', 'd:\\m', 0);"
                                 "INSERT INTO folders VALUES (1, 1, NULL, 'D:\\m', 'd:\\m', 'm');"
                                 "INSERT INTO files(id, folder_id, path, path_key, size, mtime_ns, "
                                 "status, scanned_at) VALUES (1, 1, 'D:\\m\\a.flac', "
                                 "'d:\\m\\a.flac', 1, 1, 'ok', 0);"))
                .hasValue());
    REQUIRE(
        db->execute(QStringLiteral("INSERT INTO tracks(id, file_id, search_text) VALUES (1, 1, ?)"),
                    {QStringLiteral("nier automata 遺されたもの")})
            .hasValue());
    const QString match =
        QStringLiteral("SELECT count(*) FROM tracks_fts WHERE tracks_fts MATCH ?");
    CHECK(scalar(*db, match, {QStringLiteral("\"されたも\"")}).toInt() == 1);

    REQUIRE(db->execute(QStringLiteral("UPDATE tracks SET search_text = 'other' WHERE id = 1"))
                .hasValue());
    CHECK(scalar(*db, match, {QStringLiteral("\"automata\"")}).toInt() == 0);
    REQUIRE(db->execute(QStringLiteral("DELETE FROM tracks")).hasValue());
    CHECK(scalar(*db, match, {QStringLiteral("\"other\"")}).toInt() == 0);
}
