#pragma once

// 掃描類測試共用的音樂庫：測試暫存底下的「音樂」資料夾、升到最新版的資料庫、加入那一個音樂資料夾。

#include "library/library_scanner.h"
#include "library/library_writer.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace auralis::test {

struct ScanFixture {
    QString dir;
    QString root;
    QString databasePath;
    std::unique_ptr<core::SqliteConnection> db;
    QList<core::LibraryRoot> roots;

    explicit ScanFixture(const QString& name) {
        dir = freshTestDir(name);
        root = dir + QStringLiteral("\\音樂");
        databasePath = dir + QStringLiteral("\\library.db");
        REQUIRE(core::migrateDatabase(databasePath).hasValue());
        auto opened = core::SqliteConnection::open(databasePath);
        REQUIRE(opened.hasValue());
        db = std::move(opened).value();
        core::LibraryWriter writer(*db);
        roots.append({writer.ensureRoot(root), root});
    }

    core::ScanReport scan(const core::ScanCallbacks& callbacks = {}) {
        return core::scanLibrary(*db, roots, callbacks);
    }

    /// 只回傳一個數字的查詢（例：SELECT count(*) …）。
    qint64 count(const QString& sql) {
        const auto rows = db->query(sql);
        REQUIRE(rows.hasValue());
        return rows.value().first().first().toLongLong();
    }
};

} // namespace auralis::test
