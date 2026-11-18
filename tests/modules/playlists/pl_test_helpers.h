#pragma once

// 播放清單模組測試共用：在測試暫存建一個升到最新版的資料庫，模組用自己的 ModuleDatabase（PL）。
#include "library/migrator.h"
#include "library/module_database_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <memory>

namespace auralis::test {

inline QString plTestRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

/// 測試資料庫；reopen() 換一條新的連線（模擬程式重新啟動）。
struct PlDatabase {
    QTemporaryDir dir{plTestRoot() + QStringLiteral("/t-50-pl-XXXXXX")};
    QString path;
    std::unique_ptr<core::ModuleDatabaseImpl> db;

    PlDatabase() {
        REQUIRE(dir.isValid());
        path = dir.filePath(QStringLiteral("library.db"));
        REQUIRE(core::migrateDatabase(path).hasValue());
        reopen();
    }
    void reopen() {
        db.reset();
        db = std::make_unique<core::ModuleDatabaseImpl>(QStringLiteral("PL"), path);
    }
};

} // namespace auralis::test
