// 清單的選取、拖放與右鍵選單的 C++ 部分（T-25，US-10 AC-5、US-11 AC-4 的來源端）：
// 選取（TrackSelection）、超過 5,000 首時的查詢條件格式（DragQuery）、「前往專輯」「前往藝人」
// 的曲目位置（LibraryAlbums.trackLocation）與藝人頁的開頁參數（LibraryArtists.selectArtist）。
#include "albums_controller.h"
#include "artists_controller.h"
#include "fake_playback_service.h"
#include "lib_memory_library.h"
#include "lib_test_helpers.h"
#include "search_controller.h"
#include "track_selection.h"

#include <auralis/track_drag_payload.h>

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

QList<TrackInfo> numbered(int count, int from = 0) {
    QList<TrackInfo> tracks;
    for (int i = from; i < from + count; ++i) {
        tracks.append(memTrack(1, QStringLiteral("C:/m/%1.flac").arg(i), QStringLiteral("A")));
    }
    return tracks;
}

QStringList pathsOf(const QList<TrackInfo>& tracks) {
    QStringList paths;
    for (const TrackInfo& t : tracks) {
        paths.append(t.ref.path);
    }
    return paths;
}

} // namespace

TEST_CASE("AC-11-4：查詢條件序列化後讀回一樣，空的條件省略、不是這個格式的讀不回") {
    DragQuery query;
    query.criteria.text = QStringLiteral("初音");
    query.criteria.artist = QStringLiteral("ryo");
    query.criteria.year = 2008;
    query.criteria.format = QStringLiteral("DSD64");
    query.criteria.sampleRate = 2822400;
    query.folderId = 12;
    query.albumIds = {3, 1};
    const QString json = toDragQuery(query);
    CHECK_FALSE(json.contains(QStringLiteral("\"album\"")));
    const auto back = parseDragQuery(json);
    REQUIRE(back.has_value());
    CHECK(*back == query);

    CHECK(toDragQuery({}) == QStringLiteral("{\"lib\":1}"));
    CHECK_FALSE(parseDragQuery(QStringLiteral("{\"text\":\"a\"}")).has_value());
    CHECK_FALSE(parseDragQuery(QStringLiteral("not json")).has_value());
}

TEST_CASE("DESIGN §7：選一列、Ctrl 切換、Shift 從錨點選到那列、全選；模型重設時清掉") {
    ensureApp();
    SearchResultsModel model;
    model.reset(numbered(6));
    TrackSelection selection;
    selection.setModel(&model);
    CHECK(selection.count() == 0);

    selection.select(1);
    selection.extend(3); // Shift＋下兩次
    CHECK(selection.selectedRows() == QList<int>{1, 2, 3});
    selection.extend(0); // 錨點不變，往上
    CHECK(selection.selectedRows() == QList<int>{0, 1});
    selection.toggle(4); // Ctrl＋點擊：加一列、錨點移過去
    CHECK(selection.selectedRows() == QList<int>{0, 1, 4});
    selection.toggle(0);
    CHECK(selection.selectedRows() == QList<int>{1, 4});
    selection.toggle(4);
    selection.extend(5); // 錨點是 4
    CHECK(selection.selectedRows() == QList<int>{4, 5});

    // 右鍵已選取的列保留整個選取；沒選取的列改成只選它
    selection.ensureSelected(5);
    CHECK(selection.selectedRows() == QList<int>{4, 5});
    selection.ensureSelected(2);
    CHECK(selection.selectedRows() == QList<int>{2});

    selection.selectAll();
    CHECK(selection.count() == 6);
    CHECK(selection.trackCount() == 6);
    CHECK(pathsOf(selection.selectedTracks()).first() == QStringLiteral("C:/m/0.flac"));

    // 搜尋分批接上：全選時新列也算選取
    model.append(numbered(2, 6));
    CHECK(selection.count() == 8);
    // 新查詢（模型重設）：清掉
    model.reset(numbered(3));
    CHECK(selection.count() == 0);
    CHECK_FALSE(selection.isSelected(0));
}

TEST_CASE("AC-11-4：右鍵選單的內容和拖放同一份 TrackDragPayload；只有一首時記下它的專輯與藝人") {
    ensureApp();
    SearchResultsModel model;
    QList<TrackInfo> tracks = numbered(3);
    tracks[1].albumId = 7;
    tracks[1].albumArtist = QStringLiteral("Band");
    tracks[1].ref.subsong = 2;
    model.reset(tracks);
    TrackSelection selection;
    selection.setModel(&model);
    selection.select(0);
    selection.extend(1);
    auto payload = TrackDragPayload::fromJson(selection.payloadJson().toUtf8());
    REQUIRE(payload.has_value());
    REQUIRE(payload->tracks.size() == 2);
    CHECK(payload->tracks.at(1).subsong == 2u);
    CHECK(payload->query.isEmpty());
    CHECK(selection.tracks().size() == 2);

    selection.select(1);
    selection.payloadJson();
    CHECK(TrackSelection::trackHint().albumId == 7);
    CHECK(TrackSelection::trackHint().albumArtist == QStringLiteral("Band"));
}

TEST_CASE("AC-11-4：超過 5,000 首時只帶查詢條件：全選用頁面給的條件，只選一部分照樣列出曲目") {
    ensureApp();
    SearchResultsModel model;
    model.reset(numbered(TrackDragPayload::kMaxTracks + 10));
    TrackSelection selection;
    selection.setModel(&model);
    selection.setAllQuery(
        selection.searchQuery(QStringLiteral("a"), QString(), QString(), 0, QString(), 0));
    selection.selectAll();
    CHECK(selection.queryOnly());
    auto payload = TrackDragPayload::fromJson(selection.payloadJson().toUtf8());
    REQUIRE(payload.has_value());
    CHECK(payload->tracks.isEmpty());
    const auto query = parseDragQuery(payload->query);
    REQUIRE(query.has_value());
    CHECK(query->criteria.text == QStringLiteral("a"));

    // 只選 5,001 首（不是全部）：沒有條件代表得了，列出曲目
    selection.select(0);
    selection.extend(TrackDragPayload::kMaxTracks);
    CHECK(selection.trackCount() == TrackDragPayload::kMaxTracks + 1);
    CHECK_FALSE(selection.queryOnly());
    payload = TrackDragPayload::fromJson(selection.payloadJson().toUtf8());
    REQUIRE(payload.has_value());
    CHECK(payload->tracks.size() == TrackDragPayload::kMaxTracks + 1);

    // 資料夾與專輯的條件
    CHECK(parseDragQuery(selection.folderQuery(9))->folderId == 9);
    CHECK(selection.folderQuery(0).isEmpty());
    CHECK(parseDragQuery(selection.albumQuery(4))->albumIds == QList<qint64>{4});
}

TEST_CASE("AC-11-4：還沒載完時 Shift 選滿已載入的列只帶這些曲目，Ctrl+A 才帶整份查詢條件") {
    ensureApp();
    SearchResultsModel model;
    const int loaded = TrackDragPayload::kMaxTracks + 10;
    model.reset(numbered(loaded));
    TrackSelection selection;
    selection.setModel(&model);
    selection.setAllQuery(
        selection.searchQuery(QStringLiteral("a"), QString(), QString(), 0, QString(), 0));
    selection.setComplete(false); // 後面的批次還在查

    // 手動選到所有已載入的列：沒有被選的、還沒載入的曲目不能跟著進來
    selection.select(0);
    selection.extend(loaded - 1);
    CHECK(selection.count() == loaded);
    CHECK_FALSE(selection.queryOnly());
    auto payload = TrackDragPayload::fromJson(selection.payloadJson().toUtf8());
    REQUIRE(payload.has_value());
    CHECK(payload->query.isEmpty());
    CHECK(payload->tracks.size() == loaded);

    // Ctrl+A：明確全選，代表整份查詢（含還沒載入的）
    selection.selectAll();
    CHECK(selection.queryOnly());
    payload = TrackDragPayload::fromJson(selection.payloadJson().toUtf8());
    REQUIRE(payload.has_value());
    CHECK(payload->tracks.isEmpty());
    CHECK(parseDragQuery(payload->query)->criteria.text == QStringLiteral("a"));

    // 全部載入後手動選滿：和全選是同一份曲目，用查詢條件
    selection.setComplete(true);
    selection.select(0);
    selection.extend(loaded - 1);
    CHECK(selection.queryOnly());
}

TEST_CASE("AC-11-4：專輯清單以整張專輯為單位，曲目超過 5,000 首時帶選取的 albumIds") {
    ensureApp();
    MemoryLibrary library;
    QList<TrackInfo> tracks;
    for (int i = 0; i < 3000; ++i) {
        tracks.append(memTrack(1, QStringLiteral("C:/a/%1.flac").arg(i), QStringLiteral("A")));
        tracks.append(memTrack(2, QStringLiteral("C:/b/%1.flac").arg(i), QStringLiteral("B")));
    }
    tracks.append(memTrack(3, QStringLiteral("C:/c/1.flac"), QStringLiteral("C")));
    library.set({memAlbum(1, "A", "A"), memAlbum(2, "B", "B"), memAlbum(3, "C", "C")}, tracks);
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    albums.setSort(QStringLiteral("title"));
    albums.load();
    REQUIRE(waitFor([&] { return albums.loaded(); }));
    REQUIRE(albums.count() == 3);

    TrackSelection selection;
    selection.setModel(albums.albums());
    CHECK(selection.rowTrackCount(0) == 3000);
    selection.select(2); // C：一首
    CHECK(selection.trackCount() == 1);
    CHECK(pathsOf(selection.selectedTracks()) == QStringList{"C:/c/1.flac"});
    CHECK(selection.query().isEmpty());

    selection.select(0);
    selection.extend(1); // A＋B：6,000 首
    CHECK(selection.trackCount() == 6000);
    CHECK(selection.queryOnly());
    const auto query = parseDragQuery(selection.query());
    REQUIRE(query.has_value());
    CHECK(query->albumIds == QList<qint64>{1, 2});
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("S-07：「前往專輯」「前往藝人」找得到一首的專輯與專輯的顯示藝人；不是一首時找不到") {
    ensureApp();
    MemoryLibrary library;
    library.set({memAlbum(1, "單曲", ""), memAlbum(2, "合輯", "Various Artists")},
                {memTrack(1, "C:/s/1.flac", "Solo"), memTrack(1, "C:/s/2.flac", "Guest"),
                 memTrack(2, "C:/v/1.flac", "Singer")});
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    const auto one = [](const QString& path) {
        TrackDragPayload payload;
        payload.tracks = {{path, 0}};
        return QString::fromUtf8(payload.toJson());
    };

    // 還沒讀專輯：用清單開選單時記下的那首
    SearchResultsModel model;
    TrackInfo hinted = memTrack(2, "C:/v/1.flac", "Singer");
    hinted.albumArtist = QStringLiteral("Various Artists");
    model.reset({hinted});
    TrackSelection selection;
    selection.setModel(&model);
    selection.select(0);
    QVariantMap location = albums.trackLocation(selection.payloadJson());
    CHECK(location.value("albumId").toLongLong() == 2);
    CHECK(location.value("artist").toString() == QStringLiteral("Various Artists"));

    albums.load();
    REQUIRE(waitFor([&] { return albums.loaded(); }));
    // 第二首的藝人是 Guest，但藝人頁以專輯的顯示藝人（第一首的 Solo）分組
    location = albums.trackLocation(one(QStringLiteral("C:/s/2.flac")));
    CHECK(location.value("albumId").toLongLong() == 1);
    CHECK(location.value("artist").toString() == QStringLiteral("Solo"));
    CHECK(albums.trackLocation(one(QStringLiteral("C:/x.flac"))).isEmpty());

    TrackDragPayload two;
    two.tracks = {{QStringLiteral("C:/s/1.flac"), 0}, {QStringLiteral("C:/s/2.flac"), 0}};
    CHECK(albums.trackLocation(QString::fromUtf8(two.toJson())).isEmpty());
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("S-05：藝人頁的開頁參數 {artist} 選中該藝人；曲目藝人對到所屬專輯的分組；讀完前先記住，"
          "找不到時全部藝人") {
    ensureApp();
    MemoryLibrary library;
    library.set({memAlbum(1, "A1", "Alpha"), memAlbum(2, "B1", "Beta")},
                {memTrack(1, "C:/1.flac", "Alpha"), memTrack(2, "C:/2.flac", "Beta")});
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    ArtistsController artists(albums);

    artists.selectArtist(QStringLiteral("Beta")); // 還沒讀
    artists.load();
    REQUIRE(waitFor([&] { return artists.loaded(); }));
    CHECK(artists.selectedArtist() == QStringLiteral("Beta"));
    CHECK(artists.selectedRow() == 2);
    CHECK(artists.count() == 1);

    artists.selectArtist(QStringLiteral("沒有這位"));
    CHECK(artists.selectedRow() == 0);
    CHECK(artists.count() == 2);

    artists.selectArtist(QStringLiteral("Alpha"));
    CHECK(artists.selectedRow() == 1);

    // 曲目藝人（正在播放面板給的）不是分組名稱：選有這位曲目藝人的專輯所屬的分組
    library.set({memAlbum(1, "A1", "Alpha"), memAlbum(2, "合輯", "Various Artists")},
                {memTrack(1, "C:/1.flac", "Alpha"), memTrack(2, "C:/2.flac", "Guest")});
    library.emitScanFinished();
    REQUIRE(waitFor([&] { return artists.artistCount() == 2 && !artists.busy(); }));
    artists.selectArtist(QStringLiteral("Guest"));
    CHECK(artists.selectedArtist() == QStringLiteral("Various Artists"));

    // 讀完前記住的名稱不存在：讀完後回到全部藝人
    AlbumsController albums2(library, playback, search, nullptr);
    ArtistsController artists2(albums2);
    artists2.selectArtist(QStringLiteral("沒有這位"));
    artists2.load();
    REQUIRE(waitFor([&] { return artists2.loaded(); }));
    CHECK(artists2.selectedRow() == 0);
    // 讀完前收到曲目藝人：讀完後對到所屬的分組
    AlbumsController albums3(library, playback, search, nullptr);
    ArtistsController artists3(albums3);
    artists3.selectArtist(QStringLiteral("Guest"));
    artists3.load();
    REQUIRE(waitFor([&] { return artists3.loaded(); }));
    CHECK(artists3.selectedArtist() == QStringLiteral("Various Artists"));
    albums3.shutdown();
    albums2.shutdown();
    albums.shutdown();
    search.shutdown();
}
