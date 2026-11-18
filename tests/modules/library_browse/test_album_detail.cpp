// 專輯詳細頁（S-04）的 C++ 部分（T-24，US-10 AC-4）：以 albumId 找專輯、頂部的摘要、
// 碟數與曲目藝人、播放動作、專輯被移除時的狀態。
#include "album_detail_controller.h"
#include "albums_controller.h"
#include "fake_playback_service.h"
#include "lib_memory_library.h"
#include "lib_test_helpers.h"
#include "search_controller.h"

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

TrackInfo discTrack(qint64 albumId, const QString& path, const QString& artist, int disc, int no) {
    TrackInfo t = memTrack(albumId, path, artist);
    t.discNo = disc;
    t.trackNo = no;
    return t;
}

/// 專輯 1：兩片、第二片有客串藝人；專輯 2：一片、藝人都和專輯藝人相同。
void fillLibrary(MemoryLibrary& library) {
    AlbumInfo two = memAlbum(1, "兩片", "Alpha");
    two.year = 2011;
    library.set({two, memAlbum(2, "一片", "Beta")},
                {discTrack(1, "C:/a/1-1.flac", "Alpha", 1, 1),
                 discTrack(1, "C:/a/1-2.flac", "Alpha", 1, 2),
                 discTrack(1, "C:/a/2-1.flac", "Alpha feat. Gamma", 2, 1),
                 discTrack(2, "C:/b/1.flac", "Beta", 1, 1)});
}

} // namespace

TEST_CASE("AC-10-4：專輯詳細頁從那一首開始播放，並把專輯其餘曲目放進佇列") {
    ensureApp();
    MemoryLibrary library;
    fillLibrary(library);
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    AlbumDetailController detail(albums, playback);

    detail.open(1);
    REQUIRE(waitFor([&] { return detail.loaded(); }));
    REQUIRE(detail.found());
    CHECK(detail.title() == QStringLiteral("兩片"));
    CHECK(detail.artist() == QStringLiteral("Alpha"));
    CHECK(detail.trackCount() == 3);

    CHECK(detail.play(1));
    CHECK(playback.calls() == QStringList{"playFrom 1"});
    CHECK(playback.paths() == QStringList{"C:/a/1-1.flac", "C:/a/1-2.flac", "C:/a/2-1.flac"});
    CHECK_FALSE(detail.play(3));
    CHECK_FALSE(detail.play(-1));

    // 「加到佇列最後」：整張專輯接在佇列後面，不換掉佇列
    CHECK(detail.enqueueAll());
    CHECK(playback.calls() == QStringList{"playFrom 1", "enqueueLast 3"});
    CHECK(playback.paths().size() == 6);
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("S-04：頂部摘要「年份　n 首　總長　格式摘要」，多片依碟分組，曲目藝人不同才顯示") {
    ensureApp();
    MemoryLibrary library;
    fillLibrary(library);
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    AlbumDetailController detail(albums, playback);

    detail.open(1);
    REQUIRE(waitFor([&] { return detail.loaded(); }));
    CHECK(detail.summaryText() == QStringLiteral("2011　3 首　3:00　44.1/16"));
    CHECK(detail.discCount() == 2);
    CHECK(detail.hasTrackArtists());
    REQUIRE(detail.tracks()->rowCount() == 3);
    const QModelIndex lastRow = detail.tracks()->index(2);
    CHECK(detail.tracks()->data(lastRow, SearchResultsModel::DiscNoRole).toInt() == 2);

    // 沒有年份時省略那一段；一片、藝人都相同
    detail.open(2);
    CHECK(detail.found());
    CHECK(detail.summaryText() == QStringLiteral("1 首　1:00　44.1/16"));
    CHECK(detail.discCount() == 1);
    CHECK_FALSE(detail.hasTrackArtists());
    CHECK(detail.tracks()->rowCount() == 1);
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("S-04：專輯在重新掃描後被移除時 found 變 false，播放動作不做事") {
    ensureApp();
    MemoryLibrary library;
    fillLibrary(library);
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    AlbumDetailController detail(albums, playback);

    detail.open(2);
    REQUIRE(waitFor([&] { return detail.loaded(); }));
    REQUIRE(detail.found());

    library.set({memAlbum(1, "兩片", "Alpha")}, {discTrack(1, "C:/a/1-1.flac", "Alpha", 1, 1)});
    albums.reload();
    REQUIRE(waitFor([&] { return !detail.found(); }));
    CHECK(detail.loaded());
    CHECK(detail.tracks()->rowCount() == 0);
    CHECK(detail.summaryText().isEmpty());
    CHECK_FALSE(detail.play(0));
    CHECK_FALSE(detail.enqueueAll());
    CHECK(playback.calls().isEmpty());

    // 不存在的 id 也一樣
    detail.open(99);
    CHECK_FALSE(detail.found());
    albums.shutdown();
    search.shutdown();
}
