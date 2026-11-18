// 系統媒體控制：曲目資訊的組成（音樂庫有／沒有這首、沒有封面）、晚到結果的比對、縮圖與程式圖示
// （AC-18-2；模組規格 §1 沒涵蓋的情況 1）。
#include "smtc_refresh.h"
#include "smtc_track.h"

#include <auralis/cover_service.h>
#include <auralis/library_query.h>

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>

#include <deque>
#include <utility>

using namespace auralis;
using namespace auralis::mod::smtc;

namespace {

/// 只有 findByIdentity 有內容的音樂庫。
class FakeLibrary final : public LibraryQuery {
public:
    std::optional<TrackInfo> track;
    bool fail = false;
    mutable int calls = 0;
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        ++calls;
        if (fail) {
            return Error{QStringLiteral("資料庫鎖住"), 0, QStringLiteral("findByIdentity")};
        }
        return track;
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
        return QList<TrackInfo>();
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
};

/// exportThumbnail 回傳指定的檔案路徑或錯誤，記下要的 key 與大小。
class FakeCovers final : public CoverService {
public:
    QString path;
    QStringList requested;
    int size = 0;
    /// 輸出縮圖時先呼叫（模擬輸出很慢：讀封面期間發生的事）
    std::function<void()> onExport;
    std::optional<quint32> dominantColor(const QString&) override { return std::nullopt; }
    Result<QString> exportThumbnail(const QString& key, int wanted) override {
        requested << key;
        size = wanted;
        // 這次輸出的是呼叫當下的檔案；輸出期間 path 被換掉也不影響這次
        const QString exported = path;
        if (onExport) {
            std::exchange(onExport, {})();
        }
        if (exported.isEmpty()) {
            return Error{QStringLiteral("封面讀不到"), 0, QStringLiteral("exportThumbnail")};
        }
        return exported;
    }
};

TrackInfo libraryTrack(const QString& coverKey) {
    TrackInfo info;
    info.ref = {QStringLiteral("C:\\音樂\\周杰倫\\01 晴天.flac"), 0};
    info.title = QStringLiteral("晴天");
    info.artist = QStringLiteral("周杰倫");
    info.album = QStringLiteral("葉惠美");
    info.coverKey = coverKey;
    return info;
}

} // namespace

TEST_CASE("AC-18-2：音樂庫有這首時顯示它的曲名、藝人、專輯與封面") {
    FakeLibrary library;
    library.track = libraryTrack(QStringLiteral("abc123"));
    const auto display =
        lookupDisplay(library, {QStringLiteral("C:\\音樂\\周杰倫\\01 晴天.flac"), 0});
    CHECK(display.title == QStringLiteral("晴天"));
    CHECK(display.artist == QStringLiteral("周杰倫"));
    CHECK(display.album == QStringLiteral("葉惠美"));
    CHECK(display.coverKey == QStringLiteral("abc123"));
}

TEST_CASE("AC-18-2：音樂庫沒有這首時曲名用不含副檔名的檔名，藝人、專輯、封面空白") {
    FakeLibrary library;
    const auto display = lookupDisplay(library, {QStringLiteral("D:\\拖進來\\Live 2024.mp3"), 0});
    CHECK(library.calls == 1);
    CHECK(display.title == QStringLiteral("Live 2024"));
    CHECK(display.artist.isEmpty());
    CHECK(display.album.isEmpty());
    CHECK(display.coverKey.isEmpty());
}

TEST_CASE("錯誤邊界：音樂庫查詢失敗時當成沒有這首，曲名照樣顯示") {
    FakeLibrary library;
    library.fail = true;
    const auto display = lookupDisplay(library, {QStringLiteral("D:\\a\\b.flac"), 0});
    CHECK(display.title == QStringLiteral("b"));
    CHECK(display.coverKey.isEmpty());
}

TEST_CASE("AC-18-2：有封面時讀 300px 縮圖的 PNG 內容") {
    const QString dir = QStringLiteral(AURALIS_TEST_TMP "/t-81/thumb");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/cover.png"));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("PNG-BYTES");
    file.close();

    FakeCovers covers;
    covers.path = file.fileName();
    CHECK(readThumbnail(covers, QStringLiteral("abc123")) == QByteArray("PNG-BYTES"));
    CHECK(covers.requested == QStringList{QStringLiteral("abc123")});
    CHECK(covers.size == kThumbnailSize);
}

TEST_CASE("SMTC 規格情況 1：沒有封面（key 空）時不輸出縮圖") {
    FakeCovers covers;
    covers.path = QStringLiteral("C:\\不會用到.png");
    CHECK(readThumbnail(covers, QString()).isEmpty());
    CHECK(covers.requested.isEmpty());
}

TEST_CASE("錯誤邊界：縮圖輸出失敗或檔案讀不到時回傳空的，不丟例外") {
    FakeCovers covers;
    CHECK(readThumbnail(covers, QStringLiteral("abc")).isEmpty());
    covers.path = QStringLiteral(AURALIS_TEST_TMP "/t-81/不存在.png");
    CHECK(readThumbnail(covers, QStringLiteral("abc")).isEmpty());
}

TEST_CASE("換曲很快時：晚到的結果只在和目前曲目相同時採用（路徑大小寫、分隔符不同仍算同一首）") {
    const TrackRef a{QStringLiteral("C:\\Music\\A.flac"), 0};
    CHECK(sameTrack(a, {QStringLiteral("c:/music/a.flac"), 0}));
    CHECK_FALSE(sameTrack(a, {QStringLiteral("C:\\Music\\B.flac"), 0}));
    // CUE 整軌檔的不同曲目
    CHECK_FALSE(sameTrack({QStringLiteral("C:\\x.flac"), 1}, {QStringLiteral("C:\\x.flac"), 2}));
}

TEST_CASE("SMTC 規格情況 1：程式圖示是 300px 的 PNG，深色底、中心是琥珀實心點、圓線在半徑處") {
    const QByteArray& png = appIconPng();
    REQUIRE(png.startsWith("\x89PNG"));
    QImage image;
    REQUIRE(image.loadFromData(png, "PNG"));
    CHECK(image.width() == kThumbnailSize);
    CHECK(image.height() == kThumbnailSize);
    const QColor corner = image.pixelColor(2, 2);
    const QColor center = image.pixelColor(kThumbnailSize / 2, kThumbnailSize / 2);
    // 圓的直徑是邊長 0.60：圓線在 x = 0.80 邊長
    const QColor ring = image.pixelColor(kThumbnailSize * 4 / 5, kThumbnailSize / 2);
    // 點（半徑 0.12）和圓線（半徑 0.30）之間是底色
    const QColor gap = image.pixelColor(kThumbnailSize * 71 / 100, kThumbnailSize / 2);
    CHECK(corner.lightness() < 20);
    // 琥珀 #e0a34a：紅明顯多於藍
    CHECK(center.red() > 200);
    CHECK(center.blue() < 100);
    CHECK(ring.lightness() > 200);
    CHECK(gap.lightness() < 20);
    // 同一份：只畫一次
    CHECK(&appIconPng() == &png);
}

namespace {

/// 記下顯示端目前顯示的內容。
class FakeSink final : public TrackSink {
public:
    QString title;
    QByteArray thumbnail;
    int clears = 0;
    void setTrack(const QString& value, const QString&, const QString&,
                  const QByteArray& png) noexcept override {
        title = value;
        thumbnail = png;
    }
    void setThumbnail(const QByteArray& png) noexcept override { thumbnail = png; }
    void clearTrack() noexcept override {
        ++clears;
        title.clear();
        thumbnail.clear();
    }
};

/// 手動控制的工作佇列與 UI 佇列：測試決定什麼時候執行哪個工作；UI 佇列和事件迴圈一樣先進先出。
struct ManualLoop {
    std::deque<TrackRefresher::Job> jobs;
    std::deque<std::function<void()>> ui;
    TrackRefresher::Submit submit() {
        return [this](TrackRefresher::Job job) {
            jobs.push_back(std::move(job));
        };
    }
    TrackRefresher::Post post() {
        return [this](std::function<void()> function) {
            ui.push_back(std::move(function));
        };
    }
    void runJob() {
        REQUIRE_FALSE(jobs.empty());
        auto job = std::move(jobs.front());
        jobs.pop_front();
        job([] { return false; });
    }
    void flushUi() {
        while (!ui.empty()) {
            auto function = std::move(ui.front());
            ui.pop_front();
            function();
        }
    }
};

QString coverFile(const QString& name, const QByteArray& bytes) {
    const QString dir = QStringLiteral(AURALIS_TEST_TMP "/fix-smtc-review");
    QDir().mkpath(dir);
    QFile file(dir + QLatin1Char('/') + name);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    return file.fileName();
}

} // namespace

TEST_CASE("審查擋下的競態：同一首重查時，舊工作晚到的封面不會蓋掉新結果") {
    FakeLibrary library;
    library.track = libraryTrack(QStringLiteral("old"));
    FakeCovers covers;
    covers.path = coverFile(QStringLiteral("old.png"), "OLD-COVER");
    FakeSink sink;
    ManualLoop loop;
    TrackRefresher refresher(library, covers, sink, loop.submit(), loop.post());
    const TrackRef track = library.track->ref;

    refresher.refresh(track);
    // 舊工作輸出封面很慢；這段期間重新掃描，這首已經沒有封面，新的查詢先完成並顯示程式圖示
    covers.onExport = [&] {
        library.track->coverKey.clear();
        refresher.requery(track);
        loop.runJob();
        loop.flushUi();
        CHECK(sink.thumbnail == appIconPng());
    };
    loop.runJob();
    // 舊工作最後才把舊封面排回 UI 執行緒
    loop.flushUi();
    CHECK(covers.requested == QStringList{QStringLiteral("old")});
    CHECK(sink.title == QStringLiteral("晴天"));
    CHECK(sink.thumbnail == appIconPng());
}

TEST_CASE("重新掃描後同一首換了封面：顯示新封面，舊工作的封面不會晚到蓋回去") {
    FakeLibrary library;
    library.track = libraryTrack(QStringLiteral("old"));
    FakeCovers covers;
    covers.path = coverFile(QStringLiteral("old.png"), "OLD-COVER");
    FakeSink sink;
    ManualLoop loop;
    TrackRefresher refresher(library, covers, sink, loop.submit(), loop.post());
    const TrackRef track = library.track->ref;

    refresher.refresh(track);
    covers.onExport = [&] {
        library.track->coverKey = QStringLiteral("new");
        covers.path = coverFile(QStringLiteral("new.png"), "NEW-COVER");
        refresher.requery(track);
        loop.runJob();
        loop.flushUi();
    };
    loop.runJob();
    loop.flushUi();
    CHECK(sink.thumbnail == QByteArray("NEW-COVER"));
}

TEST_CASE("換到別首或佇列清空後，前一首晚到的結果丟掉；同一首再通知一次不重查") {
    FakeLibrary library;
    FakeCovers covers;
    FakeSink sink;
    ManualLoop loop;
    TrackRefresher refresher(library, covers, sink, loop.submit(), loop.post());
    const TrackRef a{QStringLiteral("D:\\x\\A.flac"), 0};
    const TrackRef b{QStringLiteral("D:\\x\\B.flac"), 0};

    refresher.refresh(a);
    refresher.refresh(a);
    CHECK(loop.jobs.size() == 1);
    refresher.refresh(std::nullopt);
    CHECK(sink.clears == 1);
    loop.runJob();
    loop.flushUi();
    CHECK(sink.title.isEmpty());

    refresher.refresh(a);
    refresher.refresh(b);
    loop.runJob();
    loop.flushUi();
    CHECK(sink.title.isEmpty());
    loop.runJob();
    loop.flushUi();
    CHECK(sink.title == QStringLiteral("B"));
    CHECK(sink.thumbnail == appIconPng());
}

TEST_CASE("模組停止後才到的結果丟掉，不再碰顯示端") {
    FakeLibrary library;
    FakeCovers covers;
    FakeSink sink;
    ManualLoop loop;
    TrackRefresher refresher(library, covers, sink, loop.submit(), loop.post());
    refresher.refresh(TrackRef{QStringLiteral("D:\\x\\A.flac"), 0});
    loop.runJob();
    refresher.invalidate();
    loop.flushUi();
    CHECK(sink.title.isEmpty());
    refresher.refresh(TrackRef{QStringLiteral("D:\\x\\B.flac"), 0});
    CHECK(loop.jobs.empty());
}
