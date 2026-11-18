// 歌詞載入（提供給 T-66、T-67）：JobRunner 工作裡查快取 → 來源鏈 → 寫快取；換歌時舊結果不錯置；
// 工作丟例外時是錯誤狀態；真的 FLAC 的 Vorbis LYRICS 經核心的 readEmbeddedLyrics 讀到。
#include "library_fixtures.h"
#include "lyr_test_helpers.h"
#include "lyrics_loader.h"

#include "library/library_query_impl.h"
#include "library/library_scanner.h"
#include "library/sqlite_connection.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

int cacheRows(LyrDatabase& db) {
    const auto rows = db.db->query(u"SELECT COUNT(*) FROM lyrics_cache"_s);
    REQUIRE(rows.hasValue());
    return rows.value().first().first().toInt();
}

class ThrowingStep final : public LyricsStep {
public:
    LyricsSource kind() const override { return LyricsSource::Lrclib; }
    std::optional<LyricsDocument> find(const LyricsTrack&) override {
        throw std::runtime_error("故障注入");
    }
};

} // namespace

TEST_CASE("AC-15-1：在背景找到歌詞後回到 UI 執行緒，並寫進快取；第二次讀快取") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"a.flac"_s);
    writeFile(audio, "audio");
    LyrLibrary library;
    library.setEmbedded(audio, u"[00:01.00]一\n[00:03.00]二"_s);
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);
    int changes = 0;
    QObject::connect(&loader, &LyricsLoader::changed, [&] { ++changes; });

    loader.load({audio, 0});
    CHECK(loader.isLoading());
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(changes >= 2);
    CHECK(loader.track().path == audio);
    CHECK(loader.document().status == LyricsDocument::Status::Found);
    CHECK(loader.document().source == LyricsSource::Embedded);
    CHECK(loader.lineIndexAt(2000) == 0);
    CHECK(loader.lineIndexAt(3000) == 1);
    CHECK(cacheRows(db) == 1);
    CHECK(library.embeddedCalls == 1);

    loader.load({audio, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().lines.size() == 2);
    CHECK(library.embeddedCalls == 1); // 快取命中，沒有再讀檔
}

TEST_CASE("AC-15-1：沒找到也寫快取（notfound）") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"none.flac"_s);
    writeFile(audio, "audio");
    LyrLibrary library;
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);
    loader.load({audio, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().status == LyricsDocument::Status::NotFound);
    CHECK(cacheRows(db) == 1);
}

TEST_CASE("AC-15-1：內嵌歌詞暫時讀不到時不寫可重用的 notfound，檔案沒改也會重讀") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"locked.flac"_s);
    writeFile(audio, "audio");
    LyrLibrary library;
    library.setEmbedded(audio, u"[00:01.00]內嵌"_s);
    library.failEmbedded(audio, 1); // 第一次讀的時候檔案被別的程式鎖住
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);

    loader.load({audio, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().status != LyricsDocument::Status::Found);
    CHECK(loader.document().status != LyricsDocument::Status::Unavailable); // 不是線上暫時取不到
    const auto rows = db.db->query(u"SELECT status FROM lyrics_cache"_s);
    REQUIRE(rows.hasValue());
    CHECK((rows.value().isEmpty() || rows.value().first().first().toString() != u"notfound"_s));

    loader.load({audio, 0}); // 修改時間沒變
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().status == LyricsDocument::Status::Found);
    CHECK(loader.document().source == LyricsSource::Embedded);
    CHECK(library.embeddedCalls == 2);
}

TEST_CASE("換歌時舊的工作作廢，晚到的結果不會錯置到別首") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString slow = dir->filePath(u"slow.flac"_s);
    const QString fast = dir->filePath(u"fast.flac"_s);
    writeFile(slow, "audio");
    writeFile(fast, "audio");
    LyrLibrary library;
    library.setEmbedded(slow, u"慢的那首"_s);
    library.setEmbedded(fast, u"快的那首"_s);
    library.setDelay(slow, 400);
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);

    loader.load({slow, 0});
    QThread::msleep(50); // 讓慢的那首已經在讀檔
    loader.load({fast, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().lines.first().text == u"快的那首"_s);
    REQUIRE(jobs.waitForDone(5000));
    waitFor([] { return false; }, 200); // 慢的那首的結果若送回來，這段時間會處理到
    CHECK(loader.track().path == fast);
    CHECK(loader.document().lines.first().text == u"快的那首"_s);
    CHECK_FALSE(loader.isLoading());
    CHECK(cacheRows(db) == 1); // 被取消的那首不寫快取
}

TEST_CASE("錯誤邊界：來源丟例外時是錯誤狀態，程式照常；clear 回到沒有歌詞") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"boom.flac"_s);
    writeFile(audio, "audio");
    LyrLibrary library;
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);
    loader.appendSource(std::make_unique<ThrowingStep>());
    loader.load({audio, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().status == LyricsDocument::Status::Error);
    CHECK(cacheRows(db) == 0);

    loader.clear();
    CHECK(loader.document().status == LyricsDocument::Status::NotFound);
    CHECK(loader.track().path.isEmpty());
}

TEST_CASE("載入器先被刪掉時，還在跑的工作不會把結果送給已刪掉的物件") {
    ensureApp();
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"late.flac"_s);
    writeFile(audio, "audio");
    LyrLibrary library;
    library.setEmbedded(audio, u"x"_s);
    library.setDelay(audio, 200);
    JobRunner jobs;
    {
        LyricsLoader loader(jobs, library, *db.db);
        loader.load({audio, 0});
        QThread::msleep(50);
    }
    REQUIRE(jobs.waitForDone(5000));
    waitFor([] { return false; }, 100);
    SUCCEED("沒有當掉");
}

TEST_CASE("AC-15-1：真的 FLAC 的 Vorbis LYRICS 有時間標籤時是內嵌同步歌詞") {
    ensureApp();
    LyrDatabase db;
    const QString root = freshTestDir(u"lyr-embedded"_s);
    const QString synced = root + u"/synced.flac"_s;
    const QString plain = root + u"/plain.flac"_s;
    writeFlac(synced, {{u"TITLE"_s, u"同步"_s},
                       {u"LYRICS"_s, u"[00:00.50]內嵌第一行\n[00:01.25]內嵌第二行"_s}});
    writeFlac(plain, {{u"TITLE"_s, u"純文字"_s}, {u"LYRICS"_s, u"沒有時間的歌詞"_s}});
    core::LibraryQueryImpl library(db.path);
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);

    loader.load({synced, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().source == LyricsSource::Embedded);
    CHECK(loader.document().synced);
    CHECK(loader.document().lines ==
          QList<LyricLine>{{500, u"內嵌第一行"_s}, {1250, u"內嵌第二行"_s}});

    loader.load({plain, 0});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK_FALSE(loader.document().synced);
    CHECK(loader.document().lines.first().text == u"沒有時間的歌詞"_s);
}

TEST_CASE(
    "AC-15-1：CUE 曲目端到端，先找「整軌主檔名 軌號」.lrc，再借用整軌 .lrc 換成曲目內的時間") {
    ensureApp();
    LyrDatabase db;
    const QString root = freshTestDir(u"lyr-cue"_s);
    const QString dir = root + u"\\專輯"_s;
    writeFlac(dir + u"\\album.flac"_s, {}, 44100, 16, 44100 * 5); // 整軌 5 秒
    writeFile(dir + u"\\album.cue"_s, "TITLE \"cue\"\r\nFILE \"album.flac\" WAVE\r\n"
                                      "  TRACK 01 AUDIO\r\n    INDEX 01 00:00:00\r\n"
                                      "  TRACK 02 AUDIO\r\n    INDEX 01 00:02:00\r\n"
                                      "  TRACK 03 AUDIO\r\n    INDEX 01 00:03:00\r\n");
    {
        auto opened = core::SqliteConnection::open(db.path);
        REQUIRE(opened.hasValue());
        auto connection = std::move(opened).value();
        core::LibraryWriter writer(*connection);
        const QList<core::LibraryRoot> roots{{writer.ensureRoot(root), root}};
        core::scanLibrary(*connection, roots, {});
    }
    writeFile(dir + u"\\album.lrc"_s,
              "[00:00.50]一\n[00:02.50]二之一\n[00:02.90]二之二\n[00:03.50]三\n");
    core::LibraryQueryImpl library(db.path);
    const QString cue = dir + u"\\album.cue"_s;
    REQUIRE(library.audioFilePath({cue, 2}).hasValue());
    JobRunner jobs;
    LyricsLoader loader(jobs, library, *db.db);

    loader.load({cue, 2});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().source == LyricsSource::Lrc);
    CHECK(loader.document().lines == QList<LyricLine>{{500, u"二之一"_s}, {900, u"二之二"_s}});

    // 之後放進第 2 軌自己的 .lrc：快取失效，改用它（時間照原樣）。等一下讓修改時間一定晚於快取
    QThread::msleep(20);
    writeFile(dir + u"\\album 02.lrc"_s, "[00:00.10]第二軌自己的\n");
    loader.load({cue, 2});
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().lines == QList<LyricLine>{{100, u"第二軌自己的"_s}});

    loader.load({cue, 3}); // 最後一軌：整軌 .lrc 從 3 秒到檔尾
    REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    CHECK(loader.document().lines == QList<LyricLine>{{500, u"三"_s}});
}
