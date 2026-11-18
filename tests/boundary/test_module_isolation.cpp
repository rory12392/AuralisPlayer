// 模組隔離：停用測試（逐一停用每個探索到的模組）、故障注入、生命週期。
#include "fake_modules.h"
#include "modules/module_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>

using auralis::core::CoreServices;
using auralis::core::ModuleManager;
using auralis::core::ModuleStatus;
using auralis::core::registeredModules;

namespace {

QString testRoot() {
    // 測試資料只寫在 .localdata\tmp\ 底下（路徑由 CMake 編進來）
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

const ModuleStatus& statusOf(const ModuleManager& manager, const QString& code) {
    const auto& statuses = manager.statuses();
    const auto it = std::find_if(statuses.begin(), statuses.end(),
                                 [&](const ModuleStatus& s) { return s.code == code; });
    REQUIRE(it != statuses.end());
    return *it;
}

void resetCounters() {
    auralis::test::counters() = {};
}

} // namespace

TEST_CASE("隔離：探索得到靜態註冊的模組，核心沒有手寫清單") {
    const auto modules = registeredModules();
    const auto has = [&](const char* code) {
        return std::any_of(modules.begin(), modules.end(),
                           [&](const auto& m) { return m.code == QLatin1String(code); });
    };
    CHECK(has("TESTOK"));
    CHECK(has("TESTFAIL"));
}

TEST_CASE("隔離：故障注入，start() 丟例外只讓該模組失敗，其他照常") {
    resetCounters();
    QTemporaryDir dir(testRoot() + QStringLiteral("/isolation-XXXXXX"));
    REQUIRE(dir.isValid());
    CoreServices services;
    ModuleManager manager(services, dir.filePath(QStringLiteral("settings.ini")));

    REQUIRE_NOTHROW(manager.startAll());

    const auto& failed = statusOf(manager, QStringLiteral("TESTFAIL"));
    CHECK(failed.state == ModuleStatus::State::Failed);
    CHECK(failed.error.contains(QStringLiteral("故障注入")));
    CHECK(statusOf(manager, QStringLiteral("TESTOK")).state == ModuleStatus::State::Started);
    CHECK(auralis::test::counters().okHostCode == QStringLiteral("TESTOK"));

    manager.stopAll();
    CHECK(auralis::test::counters().okStopped == 1);
    // 啟動失敗的模組不呼叫 stop()
    CHECK(auralis::test::counters().failStopped == 0);
}

TEST_CASE("隔離：逐一停用每個探索到的模組，宿主都能啟動，其他模組照常") {
    for (const auto& target : registeredModules()) {
        DYNAMIC_SECTION("停用 " << target.code.toStdString()) {
            resetCounters();
            QTemporaryDir dir(testRoot() + QStringLiteral("/disable-XXXXXX"));
            REQUIRE(dir.isValid());
            const QString ini = dir.filePath(QStringLiteral("settings.ini"));
            {
                QSettings settings(ini, QSettings::IniFormat);
                settings.setValue(QStringLiteral("modules/%1/enabled").arg(target.code), false);
            }
            CoreServices services;
            ModuleManager manager(services, ini);
            REQUIRE_NOTHROW(manager.startAll());

            CHECK(statusOf(manager, target.code).state == ModuleStatus::State::Disabled);
            for (const auto& status : manager.statuses()) {
                if (status.code == target.code || status.code == QStringLiteral("TESTFAIL")) {
                    continue;
                }
                INFO("模組 " << status.code.toStdString() << "：" << status.error.toStdString());
                CHECK(status.state == ModuleStatus::State::Started);
            }
            if (target.code == QStringLiteral("TESTFAIL")) {
                CHECK(auralis::test::counters().failStarted == 0);
            }
        }
    }
}

TEST_CASE("隔離：stopAll 可以重複呼叫，解構時不重複 stop") {
    resetCounters();
    QTemporaryDir dir(testRoot() + QStringLiteral("/stop-XXXXXX"));
    REQUIRE(dir.isValid());
    {
        CoreServices services;
        ModuleManager manager(services, dir.filePath(QStringLiteral("settings.ini")));
        manager.startAll();
        manager.stopAll();
        manager.stopAll();
    }
    CHECK(auralis::test::counters().okStopped == 1);
}
