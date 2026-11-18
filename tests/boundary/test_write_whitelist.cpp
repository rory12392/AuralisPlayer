// 寫入白名單：模組只能讀寫自己擁有的資料表，碰別人的表或改 schema 丟 std::logic_error。
#include "fake_modules.h"
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "modules/module_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include <stdexcept>

using auralis::core::ModuleDatabaseImpl;

namespace {

QString testRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

struct TestDatabase {
    QTemporaryDir dir{testRoot() + QStringLiteral("/whitelist-XXXXXX")};
    QString path;
    TestDatabase() {
        REQUIRE(dir.isValid());
        path = dir.filePath(QStringLiteral("library.db"));
        REQUIRE(auralis::core::migrateDatabase(path).hasValue());
    }
};

const QString kInsertPlaylist =
    QStringLiteral("INSERT INTO playlists(name, created_at, updated_at) VALUES (?, 0, 0)");

} // namespace

TEST_CASE("隔離：寫入白名單，模組可以讀寫自己擁有的表") {
    TestDatabase db;
    ModuleDatabaseImpl playlists(QStringLiteral("PL"), db.path);

    const auto inserted = playlists.execute(kInsertPlaylist, {QStringLiteral("最愛")});
    REQUIRE(inserted.hasValue());
    CHECK(inserted.value() == 1);
    const auto rows = playlists.query(QStringLiteral("SELECT name FROM playlists"));
    REQUIRE(rows.hasValue());
    REQUIRE(rows.value().size() == 1);
    CHECK(rows.value().first().first().toString() == QStringLiteral("最愛"));

    ModuleDatabaseImpl lyrics(QStringLiteral("LYR"), db.path);
    CHECK(lyrics
              .execute(
                  QStringLiteral("INSERT INTO lyrics_cache(path_key, subsong, source, status, "
                                 "fetched_at) VALUES ('d:\\a.flac', 0, 'lrclib', 'notfound', 0)"))
              .hasValue());
    ModuleDatabaseImpl output(QStringLiteral("OUT"), db.path);
    CHECK(output
              .execute(QStringLiteral("INSERT INTO rg_computed(path_key, subsong, computed_at) "
                                      "VALUES ('d:\\a.flac', 0, 0)"))
              .hasValue());
}

TEST_CASE("隔離：寫入白名單，寫核心或別的模組的表丟 logic_error") {
    TestDatabase db;
    ModuleDatabaseImpl playlists(QStringLiteral("PL"), db.path);
    ModuleDatabaseImpl lyrics(QStringLiteral("LYR"), db.path);
    ModuleDatabaseImpl browse(QStringLiteral("LIB"), db.path);

    CHECK_THROWS_AS(playlists.execute(QStringLiteral("DELETE FROM tracks")), std::logic_error);
    CHECK_THROWS_AS(playlists.execute(QStringLiteral("UPDATE lyrics_cache SET synced = 1")),
                    std::logic_error);
    CHECK_THROWS_AS(lyrics.execute(kInsertPlaylist, {QStringLiteral("x")}), std::logic_error);
    // 沒有擁有任何表的模組什麼都不能寫
    CHECK_THROWS_AS(browse.execute(kInsertPlaylist, {QStringLiteral("x")}), std::logic_error);
    // 讀核心的表要經 LibraryQuery
    CHECK_THROWS_AS(playlists.query(QStringLiteral("SELECT count(*) FROM tracks")),
                    std::logic_error);
    // 交易裡被擋下也一樣往外丟，而且交易被還原
    CHECK_THROWS_AS(playlists.transaction([&] {
        (void)playlists.execute(kInsertPlaylist, {QStringLiteral("交易內")});
        (void)playlists.execute(QStringLiteral("DELETE FROM albums"));
        return true;
    }),
                    std::logic_error);
    const auto rows = playlists.query(QStringLiteral("SELECT count(*) FROM playlists"));
    REQUIRE(rows.hasValue());
    CHECK(rows.value().first().first().toInt() == 0);
}

TEST_CASE("隔離：寫入白名單，模組不能改 schema 或執行 PRAGMA") {
    TestDatabase db;
    ModuleDatabaseImpl playlists(QStringLiteral("PL"), db.path);
    CHECK_THROWS_AS(playlists.execute(QStringLiteral("CREATE TABLE mine(a)")), std::logic_error);
    CHECK_THROWS_AS(playlists.execute(QStringLiteral("DROP TABLE playlists")), std::logic_error);
    CHECK_THROWS_AS(playlists.execute(QStringLiteral("PRAGMA user_version = 0")), std::logic_error);
    CHECK_THROWS_AS(playlists.execute(QStringLiteral("ATTACH 'x.db' AS other")), std::logic_error);
}

TEST_CASE("隔離：寫入白名單，可預期的失敗回傳 Error 而不是例外") {
    TestDatabase db;
    ModuleDatabaseImpl playlists(QStringLiteral("PL"), db.path);
    // 違反 NOT NULL
    const auto failed =
        playlists.execute(QStringLiteral("INSERT INTO playlists(name) VALUES (NULL)"));
    CHECK_FALSE(failed.hasValue());
    // 一次多句
    CHECK_FALSE(playlists
                    .execute(kInsertPlaylist + QStringLiteral("; DELETE FROM playlists"),
                             {QStringLiteral("x")})
                    .hasValue());
    // 交易回傳 false 時還原
    const auto rolledBack = playlists.transaction([&] {
        REQUIRE(playlists.execute(kInsertPlaylist, {QStringLiteral("暫時")}).hasValue());
        return false;
    });
    REQUIRE(rolledBack.hasValue());
    CHECK_FALSE(rolledBack.value());
    CHECK(playlists.query(QStringLiteral("SELECT count(*) FROM playlists"))
              .value()
              .first()
              .first()
              .toInt() == 0);
    // 沒有資料庫時回傳 Error
    ModuleDatabaseImpl noDatabase(QStringLiteral("PL"), QString());
    CHECK_FALSE(noDatabase.execute(kInsertPlaylist, {QStringLiteral("x")}).hasValue());
}

TEST_CASE("隔離：寫入白名單，模組從 ModuleHost 拿到的資料庫與設定都屬於自己") {
    TestDatabase db;
    auralis::test::counters() = {};
    auralis::core::CoreServices services;
    services.databasePath = db.path;
    const QString settingsPath = db.dir.filePath(QStringLiteral("settings.ini"));
    {
        auralis::core::ModuleManager manager(services, settingsPath);
        manager.startAll();
    }
    // TESTOK 在 start() 裡試著寫 PL 的表（被擋下），並寫一個自己的設定
    CHECK(auralis::test::counters().okDatabase == QStringLiteral("denied"));
    CHECK(QSettings(settingsPath, QSettings::IniFormat)
              .value(QStringLiteral("TESTOK/probe"))
              .toInt() == 1);
}
