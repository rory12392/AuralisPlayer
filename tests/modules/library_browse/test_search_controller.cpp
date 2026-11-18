// SearchController 的時序：去抖 120 ms、新查詢取消舊查詢、分批載入、查詢失敗、掃描完成後重查。
#include "lib_test_helpers.h"
#include "library_fixtures.h"
#include "search_controller.h"

#include <QElapsedTimer>

using namespace auralis;
using namespace auralis::test;
using auralis::mod::lib::SearchController;
using auralis::mod::lib::SearchResultsModel;

namespace {

/// 記憶體裡的音樂庫：count 首曲名「曲目 00000」…，照 offset／limit 回傳；可以設成查詢失敗。
class FakeLibrary final : public LibraryQuery {
public:
    explicit FakeLibrary(int count) {
        for (int i = 0; i < count; ++i) {
            TrackInfo t;
            t.id = i + 1;
            t.title = QStringLiteral("曲目 %1").arg(i, 5, 10, QLatin1Char('0'));
            t.codec = QStringLiteral("FLAC");
            m_tracks.append(t);
        }
    }
    bool fail = false;
    mutable std::atomic<int> calls{0};

    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        ++calls;
        if (fail) {
            return Error{QStringLiteral("資料庫被鎖住"), 0, QStringLiteral("test")};
        }
        const int limit = filter.limit < 0 ? static_cast<int>(m_tracks.size()) : filter.limit;
        return m_tracks.mid(filter.offset, limit);
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    void emitScanFinished() { emit scanFinished(); }

private:
    QList<TrackInfo> m_tracks;
};

/// 記下每次 resultsReady 時的搜尋字串與筆數。
struct ReadyLog {
    QStringList texts;
    QList<int> counts;
    void attach(SearchController& search) {
        QObject::connect(&search, &SearchController::resultsReady, [this, &search] {
            texts.append(search.text());
            counts.append(search.count());
        });
    }
};

} // namespace

TEST_CASE("規格 §1：連續輸入時去抖 120 ms，只查最後的字串") {
    ensureApp();
    FakeLibrary library(10);
    SearchController search(library);
    ReadyLog log;
    log.attach(search);
    QElapsedTimer typing;
    typing.start();
    // 按鍵之間只處理事件、不等時間（ctest 平行時實際的等待會被拉長，超過去抖就變成另一個測試）
    for (const char* text : {"n", "ni", "nie", "nier"}) {
        search.setText(QString::fromLatin1(text));
        CHECK(search.busy());
        QCoreApplication::processEvents();
    }
    CHECK(library.calls == 0); // 還在打字，一次都沒查
    REQUIRE(waitFor([&] { return !log.texts.isEmpty(); }));
    CHECK(typing.elapsed() >= SearchController::kDebounceMs);
    CHECK(log.texts == QStringList{QStringLiteral("nier")});
    CHECK(library.calls == 1);
    CHECK_FALSE(search.busy());
    CHECK(search.elapsedMs() >= 0);
}

TEST_CASE("規格 §1：新查詢取消舊查詢，舊查詢的結果不顯示") {
    ensureApp();
    const QString dir = freshTestDir(QStringLiteral("t-20-cancel"));
    writeFlac(dir + QStringLiteral("\\音樂\\a.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("Alpha")}});
    writeFlac(dir + QStringLiteral("\\音樂\\b.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("Bravo")}});
    const QString dbPath = dir + QStringLiteral("\\library.db");
    scanIntoDatabase(dbPath, dir + QStringLiteral("\\音樂"));
    core::LibraryQueryImpl real(dbPath); // 只保存路徑的參考
    CountingLibrary library(real);
    library.delayMs = 300; // 每次查詢都慢，第二個查詢送出時第一個還在查
    SearchController search(library);
    ReadyLog log;
    log.attach(search);

    search.setText(QStringLiteral("alpha"));
    REQUIRE(waitFor([&] { return library.calls == 1; }));
    search.setText(QStringLiteral("bravo"));
    REQUIRE(waitFor([&] { return !log.texts.isEmpty() && search.complete(); }, 5000));
    waitFor([] { return false; }, 400); // 舊查詢那一批查完也不能出現
    CHECK(log.texts == QStringList{QStringLiteral("bravo")});
    REQUIRE(search.count() == 1);
    CHECK(search.results()->track(0).title == QStringLiteral("Bravo"));
    CHECK(library.calls == 2);
}

TEST_CASE("規格 §1：先顯示第一批，其餘分批接在後面，順序不變") {
    ensureApp();
    FakeLibrary library(4500);
    SearchController search(library);
    ReadyLog log;
    log.attach(search);
    search.refresh();
    REQUIRE(waitFor([&] { return !log.counts.isEmpty() && search.complete(); }));
    REQUIRE(log.counts.size() == 1);
    CHECK(log.counts.first() == SearchController::kFirstPage);
    REQUIRE(search.count() == 4500);
    for (int i : {0, 199, 200, 2199, 2200, 4499}) {
        CHECK(search.results()->track(i).title ==
              QStringLiteral("曲目 %1").arg(i, 5, 10, QLatin1Char('0')));
    }
    // 200 + 2000 + 2000 + 300：最後一批不滿就停
    CHECK(library.calls == 4);
    CHECK(search.results()
              ->data(search.results()->index(4499), SearchResultsModel::FormatRole)
              .toString() == QStringLiteral("FLAC"));
}

TEST_CASE("查詢失敗時 error 有原因、結果是空的，不會卡在載入中") {
    ensureApp();
    FakeLibrary library(5);
    library.fail = true;
    SearchController search(library);
    ReadyLog log;
    log.attach(search);
    search.setText(QStringLiteral("x"));
    REQUIRE(waitFor([&] { return !log.texts.isEmpty(); }));
    CHECK(search.error() == QStringLiteral("資料庫被鎖住"));
    CHECK(search.count() == 0);
    CHECK(search.complete());
    CHECK_FALSE(search.busy());

    library.fail = false;
    search.refresh();
    REQUIRE(waitFor([&] { return log.texts.size() == 2; }));
    CHECK(search.error().isEmpty());
    CHECK(search.count() == 5);
}

TEST_CASE("篩選選項讀取失敗時 optionsError 有原因、不會一直等；重試成功後清掉") {
    ensureApp();
    FakeLibrary library(5);
    library.fail = true;
    SearchController search(library);
    int optionsChanges = 0;
    QObject::connect(&search, &SearchController::optionsChanged, [&] { ++optionsChanges; });
    search.loadOptions();
    REQUIRE(waitFor([&] { return optionsChanges == 1; }));
    CHECK_FALSE(search.optionsError().isEmpty());
    CHECK_FALSE(search.optionsLoaded());

    library.fail = false;
    search.loadOptions();
    REQUIRE(waitFor([&] { return search.optionsLoaded(); }));
    CHECK(search.optionsError().isEmpty());
    CHECK(search.formatOptions() == QStringList{QStringLiteral("FLAC")});
}

TEST_CASE("掃描完成後重查目前的條件、重讀已讀過的選項；沒查過時不查") {
    ensureApp();
    FakeLibrary library(3);
    SearchController search(library);
    library.emitScanFinished();
    waitFor([] { return false; }, 50);
    CHECK(library.calls == 0);

    ReadyLog log;
    log.attach(search);
    int optionLoads = 0;
    QObject::connect(&search, &SearchController::optionsChanged, [&] { ++optionLoads; });
    search.refresh();
    search.loadOptions();
    REQUIRE(waitFor([&] { return log.texts.size() == 1 && optionLoads == 1; }));
    library.emitScanFinished();
    REQUIRE(waitFor([&] { return log.texts.size() == 2 && optionLoads == 2; }));
}

TEST_CASE("shutdown 之後不再交回結果") {
    ensureApp();
    FakeLibrary library(3);
    SearchController search(library);
    ReadyLog log;
    log.attach(search);
    search.refresh();
    search.shutdown();
    waitFor([] { return false; }, 100);
    CHECK(log.texts.isEmpty());
}
