// 專輯頁（S-03，T-22）的 C++ 部分：專輯與曲目的彙總（總長、格式摘要）、排序、篩選、
// 增量更新的模型（掃描中新專輯逐步出現）、檢視與排序的保存、播放整張專輯。
#include "albums_controller.h"
#include "fake_playback_service.h"
#include "lib_test_helpers.h"
#include "search_controller.h"

#include <auralis/module_settings.h>

#include <QHash>

#include <mutex>

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

TrackInfo track(qint64 albumId, const QString& path, const QString& codec, int rate, int bits,
                qint64 durationMs = 60'000) {
    TrackInfo t;
    t.albumId = albumId;
    t.ref = {path, 0};
    t.title = path;
    t.artist = QStringLiteral("藝人%1").arg(albumId);
    t.codec = codec;
    t.sampleRate = rate;
    t.bits = bits;
    t.durationMs = durationMs;
    if (codec == QLatin1String("DSD")) {
        t.dsdRate = rate;
    }
    return t;
}

AlbumInfo album(qint64 id, const QString& title, const QString& artist, int year,
                qint64 folderId = 1) {
    AlbumInfo a;
    a.id = id;
    a.title = title;
    a.albumArtist = artist;
    a.year = year;
    a.folderId = folderId;
    return a;
}

QList<qint64> idsOf(const QList<AlbumEntry>& albums) {
    QList<qint64> ids;
    for (const auto& a : albums) {
        ids.append(a.info.id);
    }
    return ids;
}

QList<qint64> idsOf(const AlbumsModel& model) {
    QList<qint64> ids;
    for (int i = 0; i < model.rowCount(); ++i) {
        ids.append(model.album(i).info.id);
    }
    return ids;
}

/// 記憶體裡的音樂庫；內容可以換（模擬掃描中寫入）。
class AlbumLibrary final : public LibraryQuery {
public:
    void set(QList<AlbumInfo> albums, QList<TrackInfo> tracks) {
        std::lock_guard lock(m_mutex);
        m_albums = std::move(albums);
        m_tracks = std::move(tracks);
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

/// 記下模型的重設、插入、移除通知次數（測試沒有連結 Qt Test，不用 QSignalSpy）。
struct ModelSignals {
    int resets = 0;
    int inserts = 0;
    int removes = 0;
    explicit ModelSignals(const QAbstractItemModel& model) {
        QObject::connect(&model, &QAbstractItemModel::modelReset, [this] { ++resets; });
        QObject::connect(&model, &QAbstractItemModel::rowsInserted, [this] { ++inserts; });
        QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [this] { ++removes; });
    }
};

class MemorySettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return m_values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { m_values[key] = value; }
    bool contains(const QString& key) const override { return m_values.contains(key); }
    void remove(const QString& key) override { m_values.remove(key); }

private:
    QHash<QString, QVariant> m_values;
};

} // namespace

TEST_CASE("S-03：封面牆的格式摘要取最高的，PCM 寫「96/24」，DSD 寫倍率，有損寫格式名") {
    CHECK(albumFormatText({track(1, "a", "FLAC", 44100, 16), track(1, "b", "FLAC", 96000, 24)}) ==
          QStringLiteral("96/24"));
    CHECK(albumFormatText({track(1, "a", "FLAC", 44100, 16)}) == QStringLiteral("44.1/16"));
    CHECK(albumFormatText({track(1, "a", "FLAC", 96000, 16), track(1, "b", "FLAC", 96000, 24)}) ==
          QStringLiteral("96/24"));
    CHECK(albumFormatText({track(1, "a", "FLAC", 192000, 24), track(1, "b", "DSD", 2822400, 1)}) ==
          QStringLiteral("DSD64"));
    CHECK(albumFormatText({track(1, "a", "MP3", 44100, 0)}) == QStringLiteral("MP3"));
    CHECK(albumFormatText({}).isEmpty());
}

TEST_CASE("S-03：專輯的總長與曲目由曲目彙總，沒有專輯藝人時用第一首的藝人") {
    const auto entries =
        buildAlbums({album(1, "A", "專輯藝人", 2020), album(2, "B", "", 2021)},
                    {track(1, "1a", "FLAC", 44100, 16, 1000), track(2, "2a", "FLAC", 44100, 16),
                     track(1, "1b", "FLAC", 44100, 16, 2500), track(0, "x", "FLAC", 44100, 16)});
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].durationMs == 3500);
    CHECK(entries[0].tracks.size() == 2);
    CHECK(entries[0].artist == QStringLiteral("專輯藝人"));
    CHECK(entries[1].artist == QStringLiteral("藝人2"));
}

TEST_CASE("模組規格 §1 情況 2：同名專輯在不同資料夾分開顯示，不合併") {
    const auto entries =
        buildAlbums({album(1, "Best", "X", 2000, 10), album(2, "Best", "X", 2000, 20)},
                    {track(1, "a/1", "FLAC", 44100, 16), track(2, "b/1", "FLAC", 96000, 24)});
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].formatText == QStringLiteral("44.1/16"));
    CHECK(entries[1].formatText == QStringLiteral("96/24"));
}

TEST_CASE("S-03：排序（加入時間新到舊、專輯名、專輯藝人、年份新到舊）") {
    auto entries = buildAlbums(
        {album(1, "b", "Z", 2001), album(2, "C", "y", 1999), album(3, "a", "x", 2010)}, {});
    sortAlbums(entries, AlbumSort::Added);
    CHECK(idsOf(entries) == QList<qint64>{3, 2, 1});
    sortAlbums(entries, AlbumSort::Title);
    CHECK(idsOf(entries) == QList<qint64>{3, 1, 2});
    sortAlbums(entries, AlbumSort::Artist);
    CHECK(idsOf(entries) == QList<qint64>{3, 2, 1});
    sortAlbums(entries, AlbumSort::Year);
    CHECK(idsOf(entries) == QList<qint64>{3, 1, 2});
    CHECK(albumSortFromName(QStringLiteral("year")) == AlbumSort::Year);
    CHECK(albumSortFromName(QStringLiteral("亂寫")) == AlbumSort::Added);
}

TEST_CASE("S-03：篩選是專輯裡有一首同時符合全部條件") {
    const auto entries = buildAlbums({album(1, "A", "X", 2020)}, {track(1, "1", "FLAC", 44100, 16),
                                                                  track(1, "2", "WAV", 96000, 24)});
    SearchCriteria c;
    CHECK(albumMatches(entries[0], c));
    c.format = QStringLiteral("WAV");
    CHECK(albumMatches(entries[0], c));
    c.sampleRate = 96000;
    CHECK(albumMatches(entries[0], c));
    c.sampleRate = 44100; // WAV 是 96 kHz、44.1 kHz 是 FLAC：沒有一首兩個都符合
    CHECK_FALSE(albumMatches(entries[0], c));
    SearchCriteria artist;
    artist.artist = QStringLiteral("藝人1");
    CHECK(albumMatches(entries[0], artist));
}

TEST_CASE("模組規格 §1 情況 3：新專輯用插入通知，已顯示的列不重建（不閃爍、不跳回頂端）") {
    ensureApp();
    AlbumsModel model;
    model.update(buildAlbums({album(1, "A", "X", 0), album(3, "C", "X", 0)}, {}));
    ModelSignals sig(model);

    model.update(buildAlbums({album(1, "A", "X", 0), album(2, "B", "X", 0), album(3, "C", "X", 0),
                              album(4, "D", "X", 0)},
                             {}));
    CHECK(idsOf(model) == QList<qint64>{1, 2, 3, 4});
    CHECK(sig.resets == 0);
    CHECK(sig.inserts == 2);
    CHECK(sig.removes == 0);

    model.update(buildAlbums({album(2, "B", "X", 0), album(4, "D", "X", 0)}, {}));
    CHECK(idsOf(model) == QList<qint64>{2, 4});
    CHECK(sig.resets == 0);
    CHECK(sig.removes == 2);

    // 順序變了（換排序）才整個重設
    model.update(buildAlbums({album(4, "D", "X", 0), album(2, "B", "X", 0)}, {}));
    CHECK(idsOf(model) == QList<qint64>{4, 2});
    CHECK(sig.resets == 1);
}

TEST_CASE(
    "AC-10-5：檢視方式與排序存在 LIB 設定，重新建立後保留；篩選跟著 LibrarySearch、不看搜尋字串") {
    ensureApp();
    AlbumLibrary library;
    library.set({album(1, "A", "X", 2020), album(2, "B", "Y", 2021)},
                {track(1, "C:/t/0.flac", "FLAC", 44100, 16),
                 track(1, "C:/t/1.flac", "FLAC", 44100, 16),
                 track(2, "C:/t/2.flac", "WAV", 96000, 24)});
    SearchController search(library);
    FakePlaybackService playback;
    MemorySettings settings;
    {
        AlbumsController albums(library, playback, search, &settings);
        CHECK(albums.view() == QStringLiteral("grid"));
        CHECK(albums.sort() == QStringLiteral("added"));
        albums.setView(QStringLiteral("detail"));
        albums.setSort(QStringLiteral("title"));
    }
    AlbumsController albums(library, playback, search, &settings);
    CHECK(albums.view() == QStringLiteral("detail"));
    CHECK(albums.sort() == QStringLiteral("title"));

    albums.load();
    REQUIRE(waitFor([&albums] { return albums.loaded(); }));
    CHECK(albums.count() == 2);
    CHECK(albums.total() == 2);
    CHECK(idsOf(*albums.albums()) == QList<qint64>{1, 2});

    search.setText(QStringLiteral("不存在的字"));
    CHECK(albums.count() == 2); // 搜尋框不影響專輯頁
    search.setFormat(QStringLiteral("WAV"));
    CHECK(albums.count() == 1);
    CHECK(albums.albumIdAt(0) == 2);
    search.clearFilters();
    CHECK(albums.count() == 2);
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("S-03：封面的播放鍵播放整張專輯，正在播放的專輯有標記") {
    ensureApp();
    AlbumLibrary library;
    library.set({album(1, "A", "X", 0), album(2, "B", "X", 0)},
                {track(1, "C:/t/0.flac", "FLAC", 44100, 16),
                 track(2, "C:/t/1.flac", "FLAC", 44100, 16),
                 track(2, "C:/t/2.flac", "FLAC", 44100, 16)});
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    albums.load();
    REQUIRE(waitFor([&albums] { return albums.loaded(); }));
    REQUIRE(albums.albumIdAt(0) == 2); // 加入時間：新的在前

    CHECK(albums.playAlbum(0));
    CHECK(playback.calls() == QStringList{QStringLiteral("playFrom 0")});
    CHECK(playback.paths() ==
          QStringList{QStringLiteral("C:/t/1.flac"), QStringLiteral("C:/t/2.flac")});
    CHECK(albums.playAlbum(0, 1));
    CHECK_FALSE(albums.playAlbum(0, 2));
    CHECK_FALSE(albums.playAlbum(5));

    playback.reset(3, -1);
    CHECK(albums.currentAlbumId() == 0);
    playback.playAt(2);
    CHECK(albums.currentAlbumId() == 2);
    playback.playAt(0);
    CHECK(albums.currentAlbumId() == 1);
    albums.shutdown();
    search.shutdown();
}

TEST_CASE("模組規格 §1 情況 3：掃描中重讀，新專輯逐步出現") {
    ensureApp();
    AlbumLibrary library;
    library.set({album(1, "A", "X", 0)}, {track(1, "C:/t/0.flac", "FLAC", 44100, 16)});
    SearchController search(library);
    FakePlaybackService playback;
    AlbumsController albums(library, playback, search, nullptr);
    albums.load();
    REQUIRE(waitFor([&albums] { return albums.loaded(); }));
    ModelSignals sig(*albums.albums());

    library.set(
        {album(1, "A", "X", 0), album(2, "B", "X", 0)},
        {track(1, "C:/t/0.flac", "FLAC", 44100, 16), track(2, "C:/t/1.flac", "FLAC", 44100, 16)});
    albums.reload();
    REQUIRE(waitFor([&albums] { return albums.count() == 2 && !albums.busy(); }));
    CHECK(sig.resets == 0);

    library.set({album(1, "A", "X", 0), album(2, "B", "X", 0), album(3, "C", "X", 0)}, {});
    emit library.scanFinished();
    REQUIRE(waitFor([&albums] { return albums.count() == 3 && !albums.busy(); }));
    CHECK(sig.resets == 0);
    CHECK(sig.inserts == 2);
    albums.shutdown();
    search.shutdown();
}
