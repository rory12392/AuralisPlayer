// 音樂資料夾服務（加入、移除、背景掃描與進度）與 LibraryQuery 的查詢。
#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/migrator.h"
#include "library_fixtures.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <type_traits>

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

struct Service {
    QString dir;
    QString root;
    QString databasePath;
    JobRunner jobs;
    LibraryQueryImpl query{databasePath};
    LibraryManager manager{jobs, query, databasePath};

    explicit Service(const QString& name) {
        ensureApp();
        dir = freshTestDir(name);
        root = dir + QStringLiteral("\\音樂");
        databasePath = dir + QStringLiteral("\\library.db");
        REQUIRE(migrateDatabase(databasePath).hasValue());
    }

    void waitIdle() {
        REQUIRE(QTest::qWaitFor([this] { return !manager.isScanning(); }, 60000));
    }
};

} // namespace

TEST_CASE("AC-08-1：加入資料夾後在背景掃描，UI 執行緒照常運作並顯示已掃描數／總數") {
    Service sv(QStringLiteral("service-scan"));
    int audioFiles = buildSampleLibrary(sv.root);
    // 多放一些檔案，讓掃描久到量得出 UI 執行緒有沒有被卡住
    const QString source = sv.root + QStringLiteral("\\專輯 A\\02.flac");
    for (int i = 0; i < 300; ++i) {
        const QString copy = sv.root + QStringLiteral("\\大量\\%1\\%2.flac").arg(i / 50).arg(i);
        writeBytes(copy, {});
        QFile::remove(copy);
        REQUIRE(QFile::copy(source, copy));
        ++audioFiles;
    }
    QSignalSpy scanFinished(&sv.query, &LibraryQuery::scanFinished);
    QSignalSpy progress(&sv.manager, &LibraryManager::progressChanged);
    QSignalSpy jobFinished(&sv.jobs, &JobRunner::finished);

    // 資料夾清單第一次讀取（開資料庫）是啟動時一次的事，不算在加入的時間裡
    REQUIRE(sv.manager.folders().isEmpty());
    QElapsedTimer call;
    call.start();
    CHECK(sv.manager.addFolder(sv.root) == LibraryManager::AddResult::Added);
    CHECK(call.elapsed() < 50); // 不在 UI 執行緒掃描
    CHECK(sv.manager.isScanning());
    CHECK(sv.manager.folders() == QStringList{sv.root});

    // UI 執行緒的計時器在掃描期間照常觸發（最長間隔代表介面卡住的時間）
    QElapsedTimer tick;
    tick.start();
    qint64 longestGap = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout,
                     [&] { longestGap = std::max(longestGap, tick.restart()); });
    timer.start(10);
    sv.waitIdle();
    timer.stop();
    INFO("UI 執行緒最長間隔 " << longestGap << " ms");
    CHECK(longestGap < 200);

    CHECK(progress.size() >= 2);
    CHECK(sv.manager.totalCount() == audioFiles);
    CHECK(sv.manager.scannedCount() == audioFiles);
    CHECK(scanFinished.size() == 1);
    REQUIRE(jobFinished.size() == 1); // 經 JobRunner
    CHECK(jobFinished.first().at(2).value<JobRunner::Outcome>() == JobRunner::Outcome::Succeeded);
    const QVariantMap report = sv.manager.lastReportMap();
    CHECK(report.value(QStringLiteral("added")).toInt() == audioFiles - 1);
    CHECK(report.value(QStringLiteral("failed")).toInt() == 1);
    CHECK(report.value(QStringLiteral("failures")).toList().size() == 1);
}

TEST_CASE("AC-08-1：加入資料夾的檢查（重複、互相包含、不存在）") {
    Service sv(QStringLiteral("service-add"));
    buildSampleLibrary(sv.root);
    REQUIRE(sv.manager.addFolder(sv.root) == LibraryManager::AddResult::Added);
    CHECK(sv.manager.addFolder(sv.root + QStringLiteral("\\")) ==
          LibraryManager::AddResult::AlreadyAdded);
    CHECK(sv.manager.addFolder(sv.root.toUpper()) == LibraryManager::AddResult::AlreadyAdded);
    CHECK(sv.manager.addFolder(sv.root + QStringLiteral("\\DSD")) ==
          LibraryManager::AddResult::Overlaps);
    CHECK(sv.manager.addFolder(sv.dir) == LibraryManager::AddResult::Overlaps);
    CHECK(sv.manager.addFolder(sv.dir + QStringLiteral("\\不存在")) ==
          LibraryManager::AddResult::NotFound);
    sv.waitIdle();

    // 重新建立服務（模擬重新啟動）：資料夾清單與最近一次報告從資料庫讀回
    LibraryManager restarted(sv.jobs, sv.query, sv.databasePath);
    CHECK(restarted.folders() == QStringList{sv.root});
    CHECK(restarted.lastReport().added == 5);
}

TEST_CASE("AC-08-1：移除音樂資料夾後曲目與專輯跟著移除，磁碟上的檔案不動") {
    Service sv(QStringLiteral("service-remove"));
    const int audioFiles = buildSampleLibrary(sv.root);
    sv.manager.addFolder(sv.root);
    sv.waitIdle();
    QSignalSpy scanFinished(&sv.query, &LibraryQuery::scanFinished);
    sv.manager.removeFolder(sv.root);
    CHECK(sv.manager.folders().isEmpty());
    sv.waitIdle();
    CHECK(scanFinished.size() == 1);
    CHECK(sv.query.tracks({}).value().isEmpty());
    CHECK(sv.query.albums().value().isEmpty());
    CHECK(sv.query.folders(0).value().isEmpty());
    CHECK(QFile::exists(sv.root + QStringLiteral("\\專輯 A\\01.flac")));
    (void)audioFiles;
}

TEST_CASE("LibraryQuery：findByIdentity、搜尋、篩選、專輯、藝人、資料夾、內嵌歌詞") {
    Service sv(QStringLiteral("service-query"));
    buildSampleLibrary(sv.root);
    sv.manager.addFolder(sv.root);
    sv.waitIdle();
    const LibraryQuery& query = sv.query;
    const QString first = sv.root + QStringLiteral("\\專輯 A\\01.flac");

    // 身分比對不分大小寫與分隔符
    auto found = query.findByIdentity(
        QString(first).replace(QLatin1Char('\\'), QLatin1Char('/')).toUpper(), 0);
    REQUIRE(found.hasValue());
    REQUIRE(found.value().has_value());
    const TrackInfo track = *found.value();
    CHECK(track.ref.path == first);
    CHECK(track.title == QStringLiteral("ＮｉｅＲ：Automata"));
    CHECK(track.codec == QStringLiteral("FLAC"));
    CHECK(track.year == 2017);
    CHECK(track.hasEmbeddedLyrics);
    CHECK(track.startCdFrame == -1);
    CHECK_FALSE(query.findByIdentity(first, 1).value().has_value());

    // AC-09-2：nier 找得到 ＮｉｅＲ：Automata；3 個字元以上走 FTS、較短的走 LIKE
    TrackFilter search;
    search.search = QStringLiteral("nier");
    auto results = query.tracks(search).value();
    REQUIRE(results.size() == 2); // 曲名與專輯名都有 NieR
    search.search = QStringLiteral("ＮＩＥＲ automata");
    CHECK(query.tracks(search).value().size() == 2);
    search.search = QStringLiteral("岡部");
    REQUIRE(query.tracks(search).value().size() == 1);
    search.search = QStringLiteral("斜光");
    CHECK(query.tracks(search).value().first().title == QStringLiteral("遺サレタ場所／斜光"));
    search.search = QStringLiteral("沒有這首");
    CHECK(query.tracks(search).value().isEmpty());

    // 篩選疊加：格式＋取樣率
    TrackFilter dsd;
    dsd.codec = QStringLiteral("DSD");
    CHECK(query.tracks(dsd).value().size() == 2);
    TrackFilter hires;
    hires.codec = QStringLiteral("FLAC");
    hires.sampleRate = 96000;
    REQUIRE(query.tracks(hires).value().size() == 1);
    TrackFilter byArtist;
    byArtist.artist = QStringLiteral("MONACA");
    byArtist.year = 2017;
    CHECK(query.tracks(byArtist).value().size() == 2);
    TrackFilter paged;
    paged.limit = 2;
    paged.offset = 1;
    CHECK(query.tracks(paged).value().size() == 2);

    // 沒有曲名標籤時用檔名
    found = query.findByIdentity(sv.root + QStringLiteral("\\DSD\\e.dff"), 0);
    CHECK(found.value()->title == QStringLiteral("e"));
    CHECK(found.value()->dsdRate == 2822400);

    const auto albums = query.albums().value();
    REQUIRE(albums.size() == 2);
    CHECK(albums.first().albumArtist == QStringLiteral("MONACA"));
    CHECK(albums.first().trackCount == 2);
    TrackFilter inAlbum;
    inAlbum.albumId = albums.first().id;
    const auto albumTracks = query.tracks(inAlbum).value();
    REQUIRE(albumTracks.size() == 2);
    CHECK(albumTracks.first().trackNo == 1);

    CHECK(query.artists().value() == QStringList{QStringLiteral("MONACA"), QStringLiteral("Tester"),
                                                 QStringLiteral("岡部啓一"),
                                                 QStringLiteral("帆足圭吾")});

    const auto roots = query.folders(0).value();
    REQUIRE(roots.size() == 1);
    CHECK(roots.first().path == sv.root);
    const auto children = query.folders(roots.first().id).value();
    CHECK(children.size() == 4);
    TrackFilter inFolder;
    inFolder.folderId = albums.first().folderId;
    CHECK(query.tracks(inFolder).value().size() == 2);

    const auto lyrics = query.readEmbeddedLyrics({first, 0});
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("歌詞"));
    CHECK(query.readEmbeddedLyrics({first, 3}).value().isEmpty());
}

TEST_CASE("T-28：資料庫路徑只收具名字串（存參考、晚綁定），暫存字串在編譯期擋掉") {
    STATIC_REQUIRE(std::is_constructible_v<LibraryQueryImpl, const QString&>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<LibraryQueryImpl, QString&&>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<LibraryQueryImpl, QString>);
    STATIC_REQUIRE_FALSE(
        std::is_constructible_v<LibraryManager, JobRunner&, LibraryQueryImpl&, QString&&>);
    STATIC_REQUIRE(
        std::is_constructible_v<LibraryManager, JobRunner&, LibraryQueryImpl&, const QString&>);
}

TEST_CASE("LibraryQuery：沒有資料庫時回傳錯誤而不是崩潰") {
    const QString empty;
    LibraryQueryImpl query(empty);
    CHECK_FALSE(query.tracks({}).hasValue());
    CHECK_FALSE(query.findByIdentity(QStringLiteral("C:\\a.flac"), 0).hasValue());
}
