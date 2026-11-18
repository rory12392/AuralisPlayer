// 專輯牆的 UI 執行緒負擔（AC-10-2 的一部分）：4,000 張專輯、4 萬首的音樂庫，
// 換排序、換篩選、掃描中重讀（新專輯逐步出現）在 UI 執行緒上的工作都要在一幀之內
// （50 fps＝20 ms），捲動中發生時才不會掉幀。讀音樂庫與組專輯在工作執行緒，只記錄耗時。
// 真正的捲動幀率要開可見視窗量（主程式的入口，見模組規格 §2「留給里程碑完整驗證」）。
#include "albums_controller.h"
#include "fake_playback_service.h"
#include "lib_test_helpers.h"
#include "search_controller.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

constexpr int kAlbums = 4000;
constexpr int kTracksPerAlbum = 10;
constexpr double kFrameMs = 20.0; // 50 fps 的一幀
constexpr int kRuns = 5;

/// 合成的音樂庫：kAlbums 張專輯、每張 kTracksPerAlbum 首，格式與取樣率輪流。
class SyntheticLibrary final : public LibraryQuery {
public:
    explicit SyntheticLibrary(int albums) { resize(albums); }

    void resize(int count) {
        std::lock_guard lock(m_mutex);
        m_albums.clear();
        m_tracks.clear();
        static const int rates[] = {44100, 48000, 96000, 192000};
        for (int a = 1; a <= count; ++a) {
            AlbumInfo album;
            album.id = a;
            album.title = QStringLiteral("專輯 %1").arg(a);
            album.albumArtist = QStringLiteral("藝人 %1").arg(a % 400);
            album.year = 1970 + a % 55;
            album.folderId = a;
            album.coverKey = QStringLiteral("%1").arg(a, 40, 16, QLatin1Char('0'));
            album.trackCount = kTracksPerAlbum;
            m_albums.append(album);
            for (int t = 1; t <= kTracksPerAlbum; ++t) {
                TrackInfo track;
                track.albumId = a;
                track.ref = {QStringLiteral("C:/lib/%1/%2.flac").arg(a).arg(t), 0};
                track.title = QStringLiteral("曲目 %1").arg(t);
                track.artist = album.albumArtist;
                track.albumArtist = album.albumArtist;
                track.album = album.title;
                track.year = album.year;
                track.trackNo = t;
                track.durationMs = 240'000;
                track.codec = a % 5 == 0 ? QStringLiteral("WAV") : QStringLiteral("FLAC");
                track.sampleRate = rates[a % 4];
                track.bits = track.sampleRate > 48000 ? 24 : 16;
                m_tracks.append(track);
            }
        }
    }
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        std::lock_guard lock(m_mutex);
        const int limit = filter.limit < 0 ? static_cast<int>(m_tracks.size()) : filter.limit;
        return m_tracks.mid(filter.offset, limit);
    }
    Result<QList<AlbumInfo>> albums() const override {
        std::lock_guard lock(m_mutex);
        return m_albums;
    }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }

private:
    mutable std::mutex m_mutex;
    QList<AlbumInfo> m_albums;
    QList<TrackInfo> m_tracks;
};

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void report(const char* what, double ms) {
    std::printf("%s：%.2f ms\n", what, ms);
    std::fflush(stdout);
}

} // namespace

TEST_CASE("AC-10-2：4,000 張專輯時換排序、篩選、掃描中重讀在 UI 執行緒都不超過一幀（20 ms）") {
    ensureApp();
    SyntheticLibrary library(kAlbums);
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);

    QElapsedTimer load;
    load.start();
    albums.load();
    REQUIRE(waitFor([&albums] { return albums.loaded(); }, 60000));
    report("讀音樂庫並組專輯（工作執行緒，含交回）", static_cast<double>(load.elapsed()));
    REQUIRE(albums.count() == kAlbums);

    // 掃描中重讀用的模型（和頁面上的一樣從 4,000 張開始，排序固定），記下有沒有整個重設
    AlbumsModel wall;
    {
        QList<AlbumEntry> initial =
            buildAlbums(library.albums().value(), library.tracks(TrackFilter{}).value());
        sortAlbums(initial, AlbumSort::Added);
        wall.update(std::move(initial));
    }
    int resets = 0;
    QObject::connect(&wall, &QAbstractItemModel::modelReset, [&resets] { ++resets; });

    std::vector<double> sortMs;
    std::vector<double> filterMs;
    std::vector<double> growMs;
    const QStringList sorts{QStringLiteral("title"), QStringLiteral("artist"),
                            QStringLiteral("year"), QStringLiteral("added")};
    int total = kAlbums;
    for (int run = 0; run <= kRuns; ++run) { // 第 0 次是暖機
        QElapsedTimer timer;
        timer.start();
        albums.setSort(sorts[run % sorts.size()]);
        const double sortElapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;

        timer.restart();
        search.setFormat(QStringLiteral("WAV"));
        const double filterElapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        CHECK(albums.count() == kAlbums / 5);
        search.setFormat(QString());
        REQUIRE(albums.count() == kAlbums);

        // 掃描中重讀、多了一張專輯：讀音樂庫與組專輯在工作執行緒（不計時）；交回 UI 執行緒後
        // 做的事（AlbumsController::apply：篩選、排序、模型的增量更新）才計時
        total += 1;
        library.resize(total);
        const QList<AlbumEntry> built =
            buildAlbums(library.albums().value(), library.tracks(TrackFilter{}).value());
        timer.restart();
        QList<AlbumEntry> shown;
        shown.reserve(built.size());
        for (const AlbumEntry& album : built) {
            if (albumMatches(album, SearchCriteria{})) {
                shown.append(album);
            }
        }
        sortAlbums(shown, AlbumSort::Added);
        wall.update(std::move(shown));
        const double growElapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        REQUIRE(wall.rowCount() == total);
        CHECK(resets == 0);

        if (run > 0) {
            sortMs.push_back(sortElapsed);
            filterMs.push_back(filterElapsed);
            growMs.push_back(growElapsed);
        }
    }
    const double sortMedian = median(sortMs);
    const double filterMedian = median(filterMs);
    const double growMedian = median(growMs);
    report("換排序（中位數）", sortMedian);
    report("換篩選（中位數）", filterMedian);
    report("掃描中重讀、多一張專輯（中位數）", growMedian);
    INFO("換排序 " << sortMedian << " ms、換篩選 " << filterMedian << " ms、重讀 " << growMedian
                   << " ms；門檻 " << kFrameMs << " ms");
    CHECK(sortMedian <= kFrameMs);
    CHECK(filterMedian <= kFrameMs);
    CHECK(growMedian <= kFrameMs);
    albums.shutdown();
    search.shutdown();
}
