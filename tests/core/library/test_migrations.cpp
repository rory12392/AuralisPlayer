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
#include <chrono>
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

namespace {

/// 用 0001 建 v1.0 的資料庫並放幾筆資料（音樂資料夾、資料夾、檔案、專輯、曲目、播放清單）。
QString buildV1Database(const QString& path) {
    const auto all = builtinMigrations();
    REQUIRE(all.size() >= 2);
    REQUIRE(migrateDatabase(path, {all.front()}).value() == 1);
    const QString data = QStringLiteral(
        "INSERT INTO library_roots VALUES (1, 'D:\\音樂', 'd:\\音樂', 5);"
        "INSERT INTO folders VALUES (1, 1, NULL, 'D:\\音樂', 'd:\\音樂', '音樂');"
        "INSERT INTO files(id, folder_id, path, path_key, size, mtime_ns, status, scanned_at) "
        "VALUES (1, 1, 'D:\\音樂\\a.flac', 'd:\\音樂\\a.flac', 1000, 7, 'ok', 9), "
        "(2, 1, 'D:\\音樂\\b.cue', 'd:\\音樂\\b.cue', 300, 8, 'ok', 9);"
        "INSERT INTO albums VALUES (1, '藝人', '專輯', 2020, 1, NULL, 3);"
        "INSERT INTO tracks(id, file_id, subsong, title, duration_ms, codec, sample_rate, "
        "album_id, search_text) VALUES (1, 1, 0, '曲一', 100, 'FLAC', 44100, 1, '曲一'), "
        "(2, 2, 1, '軌一', 2000, 'FLAC', 44100, 1, '軌一'), "
        "(3, 2, 2, '軌二', 3000, 'FLAC', 44100, 1, '軌二');"
        "INSERT INTO playlists VALUES (1, '清單', 0, 1, 1);"
        "INSERT INTO playlist_items VALUES (1, 0, 'D:\\音樂\\a.flac', 'd:\\音樂\\a.flac', 0, 0);");
    auto db = openDb(path);
    REQUIRE(db->executeScript(data).hasValue());
    return path;
}

/// v1.0 的 tracks 欄位（升級後比對既有資料用）。
QString v1TrackColumns(const QString& path) {
    auto db = openDb(path);
    const auto rows = db->query(QStringLiteral("SELECT name FROM pragma_table_info('tracks')"));
    REQUIRE(rows.hasValue());
    QStringList names;
    for (const auto& row : rows.value()) {
        names << row.first().toString();
    }
    return names.join(QStringLiteral(", "));
}

QList<QVariantList> allRows(const QString& path, const QString& sql) {
    auto db = openDb(path);
    auto rows = db->query(sql);
    REQUIRE(rows.hasValue());
    return rows.value();
}

} // namespace

TEST_CASE("AC-10-5：v1.0 資料庫升到 2：加 bitrate、added_at、meta_backfill 與索引，既有資料不變，"
          "留 bak-v1 備份") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = buildV1Database(dir.filePath(QStringLiteral("library.db")));
    const QString columns = v1TrackColumns(path);
    const QString tracksSql = QStringLiteral("SELECT %1 FROM tracks ORDER BY id").arg(columns);
    const auto tracksBefore = allRows(path, tracksSql);
    const QStringList others{"library_roots", "folders",   "files",
                             "albums",        "playlists", "playlist_items"};
    QList<QList<QVariantList>> othersBefore;
    for (const QString& table : others) {
        othersBefore << allRows(path, QStringLiteral("SELECT * FROM %1 ORDER BY 1").arg(table));
    }

    const int latest = static_cast<int>(builtinMigrations().size());
    REQUIRE(migrateDatabase(path).value() == latest);

    auto db = openDb(path);
    CHECK(scalar(*db, QStringLiteral("PRAGMA user_version")).toInt() == latest);
    for (const char* column : {"bitrate", "added_at", "meta_backfill"}) {
        INFO(column);
        const QString sql =
            QStringLiteral("SELECT count(*) FROM pragma_table_info('tracks') WHERE name = ?");
        CHECK(scalar(*db, sql, {QString::fromLatin1(column)}).toInt() == 1);
    }
    CHECK(scalar(*db, QStringLiteral("SELECT sql FROM sqlite_schema WHERE type = 'index' AND "
                                     "name = 'tracks_added_at'"))
              .toString()
              .contains(QStringLiteral("added_at")));
    // 既有資料一個值都沒變；新欄位是「待補」，兩欄空白
    CHECK(allRows(path, tracksSql) == tracksBefore);
    for (qsizetype i = 0; i < others.size(); ++i) {
        INFO(others[i].toStdString());
        CHECK(allRows(path, QStringLiteral("SELECT * FROM %1 ORDER BY 1").arg(others[i])) ==
              othersBefore[i]);
    }
    CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM tracks WHERE meta_backfill = 1 AND "
                                     "bitrate IS NULL AND added_at IS NULL"))
              .toInt() == 3);
    // 備份是升級前的 v1
    const QString backup = path + QStringLiteral(".bak-v1");
    REQUIRE(QFile::exists(backup));
    auto old = openDb(backup);
    CHECK(scalar(*old, QStringLiteral("PRAGMA user_version")).toInt() == 1);
    CHECK(scalar(*old, QStringLiteral("SELECT count(*) FROM tracks")).toInt() == 3);
    CHECK(scalar(*old, QStringLiteral("SELECT count(*) FROM pragma_table_info('tracks') WHERE "
                                      "name = 'bitrate'"))
              .toInt() == 0);
}

TEST_CASE("AC-10-5：v1.0 升級中途失敗時還原 v1 備份、回報錯誤，不帶著半套 schema（0002 也還原）") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = buildV1Database(dir.filePath(QStringLiteral("library.db")));
    const auto tracksBefore = allRows(path, QStringLiteral("SELECT * FROM tracks ORDER BY id"));

    // 故障注入：0002 之後接一條會失敗的升級
    auto migrations = builtinMigrations();
    migrations.push_back({static_cast<int>(migrations.size()) + 1,
                          QStringLiteral("9999_broken.sql"),
                          QStringLiteral("INSERT INTO no_such_table VALUES (1);")});
    const auto result = migrateDatabase(path, migrations);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("資料庫升級失敗")));
    CHECK(result.error().message.contains(QStringLiteral("已還原備份")));

    auto db = openDb(path);
    CHECK(scalar(*db, QStringLiteral("PRAGMA user_version")).toInt() == 1);
    CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM pragma_table_info('tracks') WHERE "
                                     "name IN ('bitrate', 'added_at', 'meta_backfill')"))
              .toInt() == 0);
    CHECK(scalar(*db, QStringLiteral("SELECT count(*) FROM sqlite_schema WHERE name = "
                                     "'tracks_added_at'"))
              .toInt() == 0);
    db.reset();
    CHECK(allRows(path, QStringLiteral("SELECT * FROM tracks ORDER BY id")) == tracksBefore);
    // 修好之後照常升級
    CHECK(migrateDatabase(path).value() == static_cast<int>(builtinMigrations().size()));
}

TEST_CASE("AC-10-5：資料庫版本比程式認得的新（例：v2 的資料庫給只認得 v1 的程式）時拒絕開啟") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("library.db"));
    const auto all = builtinMigrations();
    REQUIRE(migrateDatabase(path).value() == static_cast<int>(all.size()));
    // 舊版程式：只認得 0001
    const auto result = migrateDatabase(path, {all.front()});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("v%1").arg(all.size())));
    CHECK(result.error().message.contains(QStringLiteral("v1")));
    // 不降級、不動資料庫，也不產生備份
    auto db = openDb(path);
    CHECK(scalar(*db, QStringLiteral("PRAGMA user_version")).toInt() ==
          static_cast<int>(all.size()));
    CHECK_FALSE(QFile::exists(path + QStringLiteral(".bak-v%1").arg(all.size())));
}

TEST_CASE("資料庫：另一個程式升級超過 5 秒時，後開的程式等它做完，不重複備份也不回報 locked") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/migrate-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = buildV1Database(dir.filePath(QStringLiteral("library.db")));
    const QString backup = path + QStringLiteral(".bak-v1");
    QFile marker(backup); // 先開的程式做好的備份
    REQUIRE((marker.open(QIODevice::WriteOnly) && marker.write("first") == 5));
    marker.close();
    auto first = openDb(path); // 先開的程式拿到寫入鎖，升級很慢（硬碟忙碌）
    REQUIRE(first->executeScript(QStringLiteral("BEGIN IMMEDIATE;")).hasValue());
    const auto all = builtinMigrations();
    std::thread slow([&] {
        std::this_thread::sleep_for(std::chrono::seconds(6));
        QString script;
        for (size_t i = 1; i < all.size(); ++i) {
            script += all[i].sql + QStringLiteral("\n;\n");
        }
        (void)first->executeScript(
            script + QStringLiteral("PRAGMA user_version = %1; COMMIT;").arg(all.size()));
    });
    const auto result = migrateDatabase(path);
    slow.join();
    INFO((result ? QString() : result.error().message).toStdString());
    CHECK(result.hasValue());
    REQUIRE(marker.open(QIODevice::ReadOnly));
    CHECK(marker.readAll() == "first");
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
