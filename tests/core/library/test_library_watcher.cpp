// 資料夾監看（AC-08-3）與啟動時的增量檢查（AC-08-2）：在測試暫存資料夾裡新增、刪除、
// 移動檔案，音樂庫在 10 秒內跟著更新；事件去抖 2 秒；啟動時的檢查在背景、沒有變動時不改資料。
// 測試只在測試暫存資料夾的 t-10\ 底下產生的資料夾裡改檔。
#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/library_watcher.h"
#include "library/migrator.h"
#include "library_fixtures.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <functional>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

void ensureApp() {
    static int argc = 1;
    static char name[] = "auralis_core_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QCoreApplication(argc, argv); // 整個測試行程共用，不釋放
    }
}

/// 測試暫存資料夾\t-10\watch\<name>；fresh 時先清掉上一輪留下的。
QString watchTestDir(const QString& name, bool fresh) {
    const QString dir = QDir::toNativeSeparators(
        QDir::cleanPath(QStringLiteral(AURALIS_TEST_TMP "/t-10/watch/") + name));
    if (fresh) {
        QDir(dir).removeRecursively();
        REQUIRE(QDir().mkpath(dir));
    }
    return dir;
}

/// 一次「程式執行」的音樂庫服務加監看；同一個 name、fresh=false 等於重新啟動。
struct Service {
    QString dir;
    QString root;
    QString databasePath;
    JobRunner jobs;
    LibraryQueryImpl query{databasePath};
    LibraryManager manager{jobs, query, databasePath};
    LibraryWatcher watcher{manager};

    Service(const QString& name, bool fresh) {
        ensureApp();
        dir = watchTestDir(name, fresh);
        root = dir + QStringLiteral("\\音樂");
        databasePath = dir + QStringLiteral("\\library.db");
        REQUIRE(migrateDatabase(databasePath).hasValue());
    }

    void waitIdle() {
        REQUIRE(QTest::qWaitFor([this] { return !manager.isScanning(); }, 60000));
    }
    /// 加入音樂資料夾、等第一次掃描完、等監看開始收事件。
    void addAndWatch() {
        watcher.start();
        REQUIRE(manager.addFolder(root) == LibraryManager::AddResult::Added);
        waitIdle();
        REQUIRE(QTest::qWaitFor([this] { return watcher.activeFolders() == QStringList{root}; },
                                10000));
    }
    bool has(const QString& relative) {
        const auto found = query.findByIdentity(root + QLatin1Char('\\') + relative, 0);
        REQUIRE(found.hasValue());
        return found.value().has_value();
    }
    /// 等到音樂庫符合 condition，回傳花了幾毫秒（逾時回傳 -1）。
    qint64 waitLibrary(const std::function<bool()>& condition) {
        QElapsedTimer elapsed;
        elapsed.start();
        if (!QTest::qWaitFor(condition, 15000)) {
            return -1;
        }
        return elapsed.elapsed();
    }
};

} // namespace

TEST_CASE("AC-08-3：資料夾中新增、刪除、移動檔案後，音樂庫在 10 秒內更新") {
    Service sv(QStringLiteral("changes"), true);
    buildSampleLibrary(sv.root);
    sv.addAndWatch();
    REQUIRE(sv.has(QStringLiteral("專輯 A\\02.flac")));

    // 新增：複製一首到新的子資料夾
    REQUIRE(QDir().mkpath(sv.root + QStringLiteral("\\新增")));
    REQUIRE(QFile::copy(sv.root + QStringLiteral("\\專輯 A\\01.flac"),
                        sv.root + QStringLiteral("\\新增\\03.flac")));
    const qint64 added = sv.waitLibrary([&] { return sv.has(QStringLiteral("新增\\03.flac")); });
    INFO("新增後 " << added << " ms");
    CHECK(added >= 0);
    CHECK(added <= 10000);

    // 刪除
    sv.waitIdle();
    REQUIRE(QFile::remove(sv.root + QStringLiteral("\\專輯 A\\02.flac")));
    const qint64 removed =
        sv.waitLibrary([&] { return !sv.has(QStringLiteral("專輯 A\\02.flac")); });
    INFO("刪除後 " << removed << " ms");
    CHECK(removed >= 0);
    CHECK(removed <= 10000);

    // 移動到另一個子資料夾
    sv.waitIdle();
    REQUIRE(QDir().mkpath(sv.root + QStringLiteral("\\搬來")));
    REQUIRE(QFile::rename(sv.root + QStringLiteral("\\其他\\深一層\\x.wav"),
                          sv.root + QStringLiteral("\\搬來\\x.wav")));
    const qint64 moved = sv.waitLibrary([&] {
        return sv.has(QStringLiteral("搬來\\x.wav")) &&
               !sv.has(QStringLiteral("其他\\深一層\\x.wav"));
    });
    INFO("移動後 " << moved << " ms");
    CHECK(moved >= 0);
    CHECK(moved <= 10000);
}

TEST_CASE("AC-08-3：連續的變動去抖 2 秒後只掃描一次；掃描本身不會觸發監看") {
    Service sv(QStringLiteral("debounce"), true);
    buildSampleLibrary(sv.root);
    sv.addAndWatch();
    QSignalSpy scans(&sv.query, &LibraryQuery::scanFinished);

    // 第一次掃描只讀檔，不應該引起任何掃描
    QTest::qWait(LibraryWatcher::kDebounceMs + 1000);
    CHECK(scans.isEmpty());

    // 1 秒內寫 10 個檔：最後一個之後 2 秒才掃描，而且只掃一次
    const QString source = sv.root + QStringLiteral("\\專輯 A\\01.flac");
    REQUIRE(QDir().mkpath(sv.root + QStringLiteral("\\一批")));
    QElapsedTimer sinceLast;
    for (int i = 0; i < 10; ++i) {
        REQUIRE(QFile::copy(source, sv.root + QStringLiteral("\\一批\\%1.flac").arg(i)));
        sinceLast.start();
        QTest::qWait(100);
    }
    REQUIRE(QTest::qWaitFor([&] { return !scans.isEmpty(); }, 10000));
    const qint64 delay = sinceLast.elapsed();
    INFO("最後一個變動之後 " << delay << " ms 掃描完");
    CHECK(delay >= LibraryWatcher::kDebounceMs - 100);
    sv.waitIdle();
    QTest::qWait(LibraryWatcher::kDebounceMs + 1000);
    CHECK(scans.size() == 1);
    CHECK(sv.manager.lastReport().added == 10);

    // 移除音樂資料夾後不再監看
    sv.manager.removeFolder(sv.root);
    CHECK(sv.watcher.activeFolders().isEmpty());
    sv.waitIdle();
}

TEST_CASE("AC-08-2：重新啟動後在背景對已加入的資料夾做增量檢查，沒有變動時不改資料") {
    int audioFiles = 0;
    {
        Service first(QStringLiteral("startup"), true);
        audioFiles = buildSampleLibrary(first.root);
        REQUIRE(first.manager.addFolder(first.root) == LibraryManager::AddResult::Added);
        first.waitIdle();
    }

    // 沒有變動：啟動時排入檢查但不擋住呼叫端，檢查在背景完成
    {
        Service restarted(QStringLiteral("startup"), false);
        QSignalSpy scans(&restarted.query, &LibraryQuery::scanFinished);
        QElapsedTimer elapsed;
        elapsed.start();
        restarted.watcher.start();
        CHECK(restarted.manager.isScanning());
        restarted.waitIdle();
        const qint64 check = elapsed.elapsed();
        INFO("增量檢查 " << check << " ms");
        CHECK(check <= 10000);
        CHECK(scans.size() == 1);
        const ScanReport report = restarted.manager.lastReport();
        CHECK(report.scanned == audioFiles);
        CHECK(report.added == 0);
        CHECK(report.updated == 0);
        CHECK(report.removed == 0);
        CHECK(QTest::qWaitFor(
            [&] { return restarted.watcher.activeFolders() == QStringList{restarted.root}; },
            10000));
    }

    // 關著的時候刪掉一首：下次啟動的檢查把它移除
    REQUIRE(QFile::remove(watchTestDir(QStringLiteral("startup"), false) +
                          QStringLiteral("\\音樂\\專輯 A\\02.flac")));
    Service again(QStringLiteral("startup"), false);
    again.watcher.start();
    again.waitIdle();
    CHECK(again.manager.lastReport().removed == 1);
    CHECK_FALSE(again.has(QStringLiteral("專輯 A\\02.flac")));
}

TEST_CASE("AC-08-2：沒有音樂資料夾時啟動不排任何掃描") {
    Service sv(QStringLiteral("empty"), true);
    sv.watcher.start();
    CHECK_FALSE(sv.manager.isScanning());
    CHECK(sv.watcher.activeFolders().isEmpty());
}
