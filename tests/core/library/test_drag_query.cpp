// T-62：LibraryQuery::tracksForDragQuery，依 TrackDragPayload.query（只帶查詢條件的拖放）取曲目。
#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/migrator.h"
#include "library_fixtures.h"
#include "test_qt_app.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>

#include <algorithm>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

void ensureApp() {
    auralis::test::ensureQtApp("auralis_core_tests");
}

struct Library {
    QString dir;
    QString root;
    QString databasePath;
    JobRunner jobs;
    LibraryQueryImpl query{databasePath};
    LibraryManager manager{jobs, query, databasePath};

    /// ctest 每個測試案例各一個行程、會同時跑，所以每個案例用自己的資料夾。
    explicit Library(const QString& name) {
        ensureApp();
        dir = freshTestDir(QStringLiteral("drag-query-") + name);
        root = dir + QStringLiteral("\\音樂");
        databasePath = dir + QStringLiteral("\\library.db");
        REQUIRE(migrateDatabase(databasePath).hasValue());
        buildSampleLibrary(root);
        manager.addFolder(root);
        REQUIRE(QTest::qWaitFor([this] { return !manager.isScanning(); }, 60000));
    }

    QStringList titles(const QString& json) const {
        const auto tracks = query.tracksForDragQuery(json);
        REQUIRE(tracks.hasValue());
        QStringList result;
        for (const TrackInfo& track : tracks.value()) {
            result.append(track.title);
        }
        return result;
    }

    qint64 folderId(const QString& name) const {
        qint64 found = 0;
        // 先存進區域變數：range-for 不會延長 Result 暫存物件的壽命（value() 回傳它裡面的參考）
        const QList<FolderInfo> children =
            query.folders(query.folders(0).value().first().id).value();
        for (const FolderInfo& folder : children) {
            if (folder.name == name) {
                found = folder.id;
            }
        }
        REQUIRE(found != 0);
        return found;
    }

    qint64 albumId(const QString& title) const {
        qint64 found = 0;
        const QList<AlbumInfo> albums = query.albums().value();
        for (const AlbumInfo& album : albums) {
            if (album.title == title) {
                found = album.id;
            }
        }
        REQUIRE(found != 0);
        return found;
    }
};

const QString kNier = QStringLiteral("ＮｉｅＲ：Automata");
const QString kShakou = QStringLiteral("遺サレタ場所／斜光");

} // namespace

TEST_CASE("AC-11-4（T-62）：只有 lib 時是整個音樂庫，排序同 tracks()") {
    Library lib(QStringLiteral("all"));
    const auto all = lib.query.tracks({}).value();
    QStringList expected;
    for (const TrackInfo& track : all) {
        expected.append(track.title);
    }
    CHECK(lib.titles(QStringLiteral(R"({"lib":1})")) == expected);
}

TEST_CASE("AC-11-4（T-62）：text 與五種篩選同搜尋頁，格式 DSD64 換成 codec＋DSD 率") {
    Library lib(QStringLiteral("filters"));
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"text":"nier"})")) == QStringList{kNier, kShakou});
    CHECK(lib.titles(
              QStringLiteral(R"({"lib":1,"artist":"MONACA","year":2017,"sampleRate":96000})")) ==
          QStringList{kShakou});
    CHECK(
        lib.titles(QStringLiteral(
                       R"({"lib":1,"format":"FLAC","album":"NieR：Automata Original Soundtrack"})"))
            .size() == 2);
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"format":"DSD64"})")).size() == 2);
    // DSD64 和另外選的 44.1 kHz 互相矛盾：空清單，不是錯誤
    const auto contradiction = lib.query.tracksForDragQuery(
        QStringLiteral(R"({"lib":1,"format":"DSD64","sampleRate":44100})"));
    REQUIRE(contradiction.hasValue());
    CHECK(contradiction.value().isEmpty());
}

TEST_CASE("AC-11-4（T-62）：folderId 含子資料夾") {
    Library lib(QStringLiteral("folder"));
    // 「其他」底下只有子資料夾「深一層」裡的 x.wav
    const qint64 other = lib.folderId(QStringLiteral("其他"));
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"folderId":%1})").arg(other)) ==
          QStringList{QStringLiteral("Wave")});
    // 音樂資料夾本身＝全部
    const qint64 root = lib.query.folders(0).value().first().id;
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"folderId":%1})").arg(root)).size() ==
          lib.query.tracks({}).value().size());
    // 和其他條件取交集
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"folderId":%1,"text":"nier"})").arg(root)) ==
          QStringList{kNier, kShakou});
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"folderId":%1,"text":"nier"})").arg(other))
              .isEmpty());
}

TEST_CASE("AC-11-4（T-62）：albumIds 依給的順序＋碟號＋軌號，和其他條件取交集") {
    Library lib(QStringLiteral("albums"));
    const qint64 nier = lib.albumId(QStringLiteral("NieR：Automata Original Soundtrack"));
    const qint64 dsd = lib.albumId(QStringLiteral("DSD 測試"));
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"albumIds":[%1,%2]})").arg(dsd).arg(nier)) ==
          QStringList{QStringLiteral("DSF 曲"), kNier, kShakou});
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"albumIds":[%1,%2]})").arg(nier).arg(dsd)) ==
          QStringList{kNier, kShakou, QStringLiteral("DSF 曲")});
    // 重複的 id 不重複列曲目
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"albumIds":[%1,%1]})").arg(nier)).size() == 2);
    // 交集：專輯＋搜尋、專輯＋資料夾
    CHECK(lib.titles(
              QStringLiteral(R"({"lib":1,"albumIds":[%1,%2],"text":"斜光"})").arg(dsd).arg(nier)) ==
          QStringList{kShakou});
    const qint64 dsdFolder = lib.folderId(QStringLiteral("DSD"));
    CHECK(lib.titles(QStringLiteral(R"({"lib":1,"albumIds":[%1,%2],"folderId":%3})")
                         .arg(nier)
                         .arg(dsd)
                         .arg(dsdFolder)) == QStringList{QStringLiteral("DSF 曲")});
}

TEST_CASE("AC-11-4（T-62）：不是查詢條件時回傳錯誤、不丟例外") {
    Library lib(QStringLiteral("errors"));
    for (const QString& bad : {QStringLiteral(R"({"lib":2})"), QStringLiteral(R"({"text":"nier"})"),
                               QStringLiteral("壞掉的 {"), QStringLiteral("[1,2]"), QString()}) {
        INFO(bad.toStdString());
        CHECK_FALSE(lib.query.tracksForDragQuery(bad).hasValue());
    }
}

// 手動量測（隱藏，ctest 不跑）：AURALIS_PERF_DRAG_DB 指向已掃好 4 萬首的 library.db 複本。
// 全部、音樂資料夾（含子資料夾）、全部專輯 id 各量 5 次取中位數。T-62 量過一次，數字在 dev-log。
TEST_CASE("T-62 量測：4 萬首的 tracksForDragQuery", "[.perf]") {
    const QString databasePath = qEnvironmentVariable("AURALIS_PERF_DRAG_DB");
    if (databasePath.isEmpty()) {
        SKIP("沒設 AURALIS_PERF_DRAG_DB");
    }
    LibraryQueryImpl query(databasePath);
    QStringList albumIds;
    const QList<AlbumInfo> albums = query.albums().value();
    for (const AlbumInfo& album : albums) {
        albumIds.append(QString::number(album.id));
    }
    const QString cases[] = {
        QStringLiteral(R"({"lib":1})"),
        QStringLiteral(R"({"lib":1,"folderId":%1})").arg(query.folders(0).value().first().id),
        QStringLiteral(R"({"lib":1,"albumIds":[%1]})").arg(albumIds.join(QLatin1Char(','))),
    };
    for (const QString& json : cases) {
        query.tracksForDragQuery(json); // 暖機
        QList<qint64> times;
        qsizetype count = 0;
        for (int i = 0; i < 5; ++i) {
            QElapsedTimer timer;
            timer.start();
            const auto tracks = query.tracksForDragQuery(json);
            times.append(timer.elapsed());
            REQUIRE(tracks.hasValue());
            count = tracks.value().size();
        }
        std::sort(times.begin(), times.end());
        WARN(json.left(40).toStdString()
             << "…：" << count << " 首，中位數 " << times.at(2) << " ms");
    }
}

TEST_CASE("T-62：LibraryQuery 基底的預設實作回傳不支援（測試替身不用實作）") {
    struct Bare final : LibraryQuery {
        Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
            return std::optional<TrackInfo>();
        }
        Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
            return QList<TrackInfo>();
        }
        Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
        Result<QStringList> artists() const override { return QStringList(); }
        Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
        Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    } bare;
    CHECK_FALSE(bare.tracksForDragQuery(QStringLiteral(R"({"lib":1})")).hasValue());
}
