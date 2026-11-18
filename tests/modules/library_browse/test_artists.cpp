// 藝人頁（S-05）的 C++ 部分（T-23，US-10 AC-3）：藝人分組（模組規格 §1 情況 4）、
// 左欄的增量更新、選取與播放。
#include "albums_controller.h"
#include "artists_controller.h"
#include "fake_playback_service.h"
#include "lib_memory_library.h"
#include "lib_test_helpers.h"
#include "search_controller.h"

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

QStringList namesOf(const ArtistsModel& model) {
    QStringList names;
    for (int row = 1; row < model.rowCount(); ++row) {
        names.append(model.artist(row).name);
    }
    return names;
}

QStringList titlesOf(const AlbumsModel& model) {
    QStringList titles;
    for (int i = 0; i < model.rowCount(); ++i) {
        titles.append(model.album(i).info.title);
    }
    return titles;
}

} // namespace

TEST_CASE(
    "模組規格 §1 情況 4：以專輯藝人分組，沒有專輯藝人時用第一首的藝人，Various Artists 不拆開") {
    const QList<AlbumEntry> albums = buildAlbums(
        {memAlbum(1, "合輯", "Various Artists"), memAlbum(2, "單曲", ""),
         memAlbum(3, "B 面", "beta"), memAlbum(4, "A 面", "Alpha"), memAlbum(5, "無名", "")},
        {memTrack(1, "C:/v/1.flac", "Singer A"), memTrack(1, "C:/v/2.flac", "Singer B"),
         memTrack(2, "C:/s/1.flac", "Solo"), memTrack(2, "C:/s/2.flac", "Guest"),
         memTrack(3, "C:/b/1.flac", "beta"), memTrack(4, "C:/a/1.flac", "Alpha"),
         memTrack(5, "C:/n/1.flac", "")});
    const QList<ArtistEntry> artists = groupArtists(albums);
    QStringList names;
    for (const ArtistEntry& a : artists) {
        names.append(a.name);
    }
    // 不分大小寫排序，沒有藝人資料的排最後；合輯不拆成 Singer A／Singer B，單曲用第一首的 Solo
    CHECK(names == QStringList{QStringLiteral("Alpha"), QStringLiteral("beta"),
                               QStringLiteral("Solo"), QStringLiteral("Various Artists"),
                               QString()});
    CHECK(artists.at(3).albumCount == 1);
    CHECK(albumsOfArtist(albums, QStringLiteral("Various Artists")).size() == 1);
    CHECK(albumsOfArtist(albums, QStringLiteral("Singer A")).isEmpty());
}

TEST_CASE("S-05：藝人清單用插入與移除通知更新，第 0 列是全部藝人") {
    ensureApp();
    ArtistsModel model;
    model.update({{QStringLiteral("A"), 1}, {QStringLiteral("C"), 2}}, 3);
    REQUIRE(model.rowCount() == 3);
    CHECK(model.data(model.index(0), ArtistsModel::AllRole).toBool());
    CHECK(model.data(model.index(0), ArtistsModel::AlbumCountRole).toInt() == 3);

    ModelLog log(model);
    model.update({{QStringLiteral("B"), 1}, {QStringLiteral("C"), 3}, {QStringLiteral("D"), 1}}, 5);
    CHECK(namesOf(model) == QStringList{"B", "C", "D"});
    CHECK(log.resets == 0);
    CHECK(log.inserts == 2);
    CHECK(log.removes == 1);
    CHECK(model.data(model.index(2), ArtistsModel::AlbumCountRole).toInt() == 3);
    CHECK(model.data(model.index(0), ArtistsModel::AlbumCountRole).toInt() == 5);
    CHECK(model.rowOf(QStringLiteral("D")) == 3);
    CHECK(model.rowOf(QStringLiteral("A")) == -1);
}

TEST_CASE("S-05：選藝人顯示他的專輯，播放整張專輯；重讀後選取保留，藝人不在了回到全部藝人") {
    ensureApp();
    MemoryLibrary library;
    library.set({memAlbum(1, "A1", "Alpha"), memAlbum(2, "B1", "Beta"), memAlbum(3, "A2", "Alpha")},
                {memTrack(1, "C:/1.flac", "Alpha"), memTrack(2, "C:/2.flac", "Beta"),
                 memTrack(3, "C:/3a.flac", "Alpha"), memTrack(3, "C:/3b.flac", "Alpha")});
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    ArtistsController artists(albums);
    artists.load();
    REQUIRE(waitFor([&] { return artists.loaded(); }));
    CHECK(namesOf(*artists.artists()) == QStringList{"Alpha", "Beta"});
    CHECK(artists.selectedRow() == 0);
    CHECK(artists.count() == 3); // 全部藝人

    artists.select(1);
    CHECK(artists.selectedArtist() == QStringLiteral("Alpha"));
    CHECK(titlesOf(*artists.albums()) == QStringList{"A2", "A1"}); // 排序跟專輯頁（加入時間）
    albums.setSort(QStringLiteral("title"));
    CHECK(titlesOf(*artists.albums()) == QStringList{"A1", "A2"});

    CHECK(artists.playAlbum(1, 1));
    CHECK(playback.paths() == QStringList{"C:/3a.flac", "C:/3b.flac"});
    CHECK(playback.calls() == QStringList{"playFrom 1"});
    CHECK(artists.albumIdAt(0) == 1);
    CHECK_FALSE(artists.playAlbum(2));

    // 掃描後多了一位排在前面的藝人：選取（名稱）不變、列號跟著變
    library.set({memAlbum(1, "A1", "Alpha"), memAlbum(2, "B1", "Beta"), memAlbum(3, "A2", "Alpha"),
                 memAlbum(4, "0", "Aaron")},
                {memTrack(1, "C:/1.flac", "Alpha"), memTrack(2, "C:/2.flac", "Beta"),
                 memTrack(3, "C:/3a.flac", "Alpha"), memTrack(4, "C:/4.flac", "Aaron")});
    library.emitScanFinished();
    REQUIRE(waitFor([&] { return artists.artistCount() == 3 && !artists.busy(); }));
    CHECK(artists.selectedArtist() == QStringLiteral("Alpha"));
    CHECK(artists.selectedRow() == 2);

    // Alpha 不見了：回到全部藝人
    library.set({memAlbum(2, "B1", "Beta")}, {memTrack(2, "C:/2.flac", "Beta")});
    library.emitScanFinished();
    REQUIRE(waitFor([&] { return artists.artistCount() == 1 && !artists.busy(); }));
    CHECK(artists.selectedRow() == 0);
    CHECK(artists.count() == 1);
    albums.shutdown();
    search.shutdown();
}
