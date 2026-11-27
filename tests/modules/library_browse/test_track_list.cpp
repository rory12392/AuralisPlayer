// 「所有曲目」頁（S-07，T-21）用的 C++ 部分：清單列的文字（長度、取樣率／位元）、模型的顯示角色、
// 從某一首開始播放與正在播放的標記（LibraryPlayback）。T-115：位元率與加入時間兩欄的文字、角色與排序。
#include "fake_playback_service.h"
#include "lib_test_helpers.h"
#include "library_playback.h"
#include "search_controller.h"
#include "track_text.h"

#include <QDateTime>

#include <mutex>

using namespace auralis;
using namespace auralis::test;
using auralis::mod::lib::LibraryPlayback;
using auralis::mod::lib::SearchController;
using auralis::mod::lib::SearchResultsModel;

namespace {

TrackInfo makeTrack(const QString& path, const QString& codec, int rate, int bits,
                    int dsdRate = 0) {
    TrackInfo t;
    t.ref = {path, 0};
    t.title = path;
    t.codec = codec;
    t.sampleRate = rate;
    t.bits = bits;
    t.dsdRate = dsdRate;
    return t;
}

/// 記憶體裡的音樂庫：照 offset／limit 回傳固定的曲目。
class ListLibrary final : public LibraryQuery {
public:
    explicit ListLibrary(QList<TrackInfo> tracks) : m_tracks(std::move(tracks)) {}

    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        {
            std::lock_guard lock(m_mutex);
            m_lastFilter = filter;
        }
        const int limit = filter.limit < 0 ? static_cast<int>(m_tracks.size()) : filter.limit;
        return m_tracks.mid(filter.offset, limit);
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    /// 最近一次 tracks() 收到的條件（T-115：排序要交給 LibraryQuery）。
    TrackFilter lastFilter() const {
        std::lock_guard lock(m_mutex);
        return m_lastFilter;
    }

private:
    QList<TrackInfo> m_tracks;
    mutable std::mutex m_mutex;
    mutable TrackFilter m_lastFilter;
};

} // namespace

TEST_CASE("S-07：長度寫成 m:ss，超過一小時 h:mm:ss，不知道時「—」") {
    using auralis::mod::lib::durationText;
    CHECK(durationText(0) == QStringLiteral("0:00"));
    CHECK(durationText(65'400) == QStringLiteral("1:05"));
    CHECK(durationText(3'725'000) == QStringLiteral("1:02:05"));
    CHECK(durationText(-1) == QStringLiteral("—"));
}

TEST_CASE("S-07：取樣率／位元寫成「96 kHz / 24 bit」，有損格式只寫取樣率，DSD 寫倍率") {
    using auralis::mod::lib::qualityText;
    CHECK(qualityText(makeTrack("a", "FLAC", 96000, 24)) == QStringLiteral("96 kHz / 24 bit"));
    CHECK(qualityText(makeTrack("b", "FLAC", 44100, 16)) == QStringLiteral("44.1 kHz / 16 bit"));
    CHECK(qualityText(makeTrack("c", "MP3", 44100, 0)) == QStringLiteral("44.1 kHz"));
    CHECK(qualityText(makeTrack("d", "DSD", 2822400, 1, 2822400)) == QStringLiteral("DSD64"));
    CHECK(qualityText(makeTrack("e", "FLAC", 0, 0)).isEmpty());
}

TEST_CASE("S-07：清單模型提供長度、取樣率／位元與封面網址的顯示文字") {
    SearchResultsModel model;
    TrackInfo withCover = makeTrack("C:/m/1.flac", "FLAC", 96000, 24);
    withCover.durationMs = 65'400;
    withCover.coverKey = QStringLiteral("abc");
    model.reset({withCover, makeTrack("C:/m/2.mp3", "MP3", 44100, 0)});
    const auto roles = model.roleNames();
    const auto role = [&roles](const char* name) {
        return roles.key(QByteArray(name));
    };
    CHECK(model.data(model.index(0), role("durationText")).toString() == QStringLiteral("1:05"));
    CHECK(model.data(model.index(0), role("qualityText")).toString() ==
          QStringLiteral("96 kHz / 24 bit"));
    CHECK(model.data(model.index(0), role("coverUrl")).toString() ==
          QStringLiteral("image://cover/abc"));
    CHECK(model.data(model.index(1), role("coverUrl")).toString().isEmpty());
}

TEST_CASE("S-07：雙擊或 Enter 從這首開始播放，佇列是整份清單") {
    ensureApp();
    ListLibrary library({makeTrack("C:/m/0.flac", "FLAC", 44100, 16),
                         makeTrack("C:/m/1.flac", "FLAC", 44100, 16),
                         makeTrack("C:/m/2.flac", "FLAC", 44100, 16)});
    SearchController search(library);
    FakePlaybackService playback;
    LibraryPlayback player(playback, search);
    search.refresh();
    REQUIRE(waitFor([&search] { return search.complete() && search.count() == 3; }));

    CHECK(player.playFrom(1));
    CHECK(playback.calls() == QStringList{QStringLiteral("playFrom 1")});
    CHECK(playback.paths() == QStringList{QStringLiteral("C:/m/0.flac"),
                                          QStringLiteral("C:/m/1.flac"),
                                          QStringLiteral("C:/m/2.flac")});
    CHECK_FALSE(player.playFrom(3));
    CHECK_FALSE(player.playFrom(-1));
    search.shutdown();
}

TEST_CASE("S-07：正在播放的曲目由 currentKey 標記，換曲時更新") {
    ensureApp();
    ListLibrary library({});
    SearchController search(library);
    FakePlaybackService playback;
    playback.reset(3, -1);
    LibraryPlayback player(playback, search);
    CHECK(player.currentKey().isEmpty());
    playback.playAt(2);
    CHECK(player.currentKey() == LibraryPlayback::keyOf(QStringLiteral("C:/t/2.flac"), 0));
    CHECK(LibraryPlayback::keyOf(QStringLiteral("a"), 1) !=
          LibraryPlayback::keyOf(QStringLiteral("a"), 0));
    search.shutdown();
}

TEST_CASE("AC-10-5：位元率寫成「1,411 kbps」，加入時間是本地時區的日期，不知道時空白") {
    using auralis::mod::lib::addedText;
    using auralis::mod::lib::bitrateText;
    CHECK(bitrateText(1411) == QStringLiteral("1,411 kbps"));
    CHECK(bitrateText(320) == QStringLiteral("320 kbps"));
    CHECK(bitrateText(std::nullopt).isEmpty());
    const qint64 ms = QDateTime(QDate(2026, 11, 26), QTime(23, 30)).toMSecsSinceEpoch();
    CHECK(addedText(ms) == QStringLiteral("2026-11-26")); // 本地 23:30，不是 UTC 的日期
    CHECK(addedText(std::nullopt).isEmpty());
}

TEST_CASE("AC-10-5：清單模型提供位元率與加入時間（值與顯示文字），沒有值時空白") {
    SearchResultsModel model;
    TrackInfo known = makeTrack("C:/m/1.mp3", "MP3", 44100, 0);
    known.bitrate = 320;
    known.addedAt = QDateTime(QDate(2025, 1, 2), QTime(8, 0)).toMSecsSinceEpoch();
    model.reset({known, makeTrack("C:/m/2.flac", "FLAC", 44100, 16)});
    const auto roles = model.roleNames();
    const auto value = [&](int row, const char* name) {
        return model.data(model.index(row), roles.key(QByteArray(name)));
    };
    CHECK(value(0, "bitrate").toInt() == 320);
    CHECK(value(0, "addedAt").toLongLong() == known.addedAt.value());
    CHECK(value(0, "bitrateText").toString() == QStringLiteral("320 kbps"));
    CHECK(value(0, "addedText").toString() == QStringLiteral("2025-01-02"));
    // 補值中或未知：值是 undefined、文字空白
    CHECK_FALSE(value(1, "bitrate").isValid());
    CHECK_FALSE(value(1, "addedAt").isValid());
    CHECK(value(1, "bitrateText").toString().isEmpty());
    CHECK(value(1, "addedText").toString().isEmpty());
}

TEST_CASE("AC-10-5：依位元率或加入時間排序時查詢帶排序鍵與升降冪，換排序重查、清除搜尋不清排序") {
    ensureApp();
    ListLibrary library({makeTrack("C:/m/0.flac", "FLAC", 44100, 16)});
    SearchController search(library);
    int ready = 0;
    QObject::connect(&search, &SearchController::resultsReady, [&ready] { ++ready; });
    const auto waitQuery = [&] {
        const int before = ready;
        REQUIRE(waitFor([&] { return ready > before && search.complete(); }));
    };
    int sortChanges = 0;
    QObject::connect(&search, &SearchController::sortChanged, [&] { ++sortChanges; });

    search.refresh();
    waitQuery();
    CHECK(search.sort().isEmpty());
    CHECK(library.lastFilter().sort == TrackSort::Default);

    search.sortBy(QStringLiteral("bitrate"), false);
    waitQuery();
    CHECK(search.sort() == QStringLiteral("bitrate"));
    CHECK_FALSE(search.descending());
    CHECK(library.lastFilter().sort == TrackSort::Bitrate);
    CHECK_FALSE(library.lastFilter().descending);

    search.sortBy(QStringLiteral("addedAt"), true);
    waitQuery();
    CHECK(library.lastFilter().sort == TrackSort::AddedAt);
    CHECK(library.lastFilter().descending);
    CHECK(sortChanges == 2);

    // 搜尋與篩選換了，排序照舊
    search.setFormat(QStringLiteral("FLAC"));
    waitQuery();
    search.clear();
    waitQuery();
    CHECK(search.sort() == QStringLiteral("addedAt"));
    CHECK(library.lastFilter().sort == TrackSort::AddedAt);

    // 回到預設順序；不認得的鍵也當預設
    search.sortBy(QString(), false);
    waitQuery();
    CHECK(library.lastFilter().sort == TrackSort::Default);
    CHECK_FALSE(library.lastFilter().descending);
    search.sortBy(QStringLiteral("title"), true);
    CHECK(search.sort().isEmpty());
    CHECK_FALSE(search.descending());
    search.shutdown();
}
