// LRCLIB 那一段（T-67，US-16）：請求的網址與 User-Agent、/api/get → 404 → /api/search 的選擇、
// 逾時與壞掉的回應當成暫時取不到（AC-16-2）、關閉時不連線（AC-16-3）、另存 .lrc（AC-16-4）、
// 版本後綴與純音樂（模組規格「沒涵蓋的情況」第 1、2 條）。全部對本機的假伺服器，不打真的 LRCLIB。
#include "fake_lrclib_server.h"
#include "lrclib.h"
#include "lyr_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QFile>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

const QByteArray kSyncedRecord =
    R"({"id":1,"trackName":"A","duration":215,"instrumental":false,)"
    R"("plainLyrics":"第一行\n第二行","syncedLyrics":"[00:01.00]第一行\n[00:02.50]第二行"})";

LyricsTrack sampleTrack(const QString& title = u"雨の日 + A&B"_s) {
    LyricsTrack t;
    t.ref = {u"C:\\music\\a.flac"_s, 0};
    t.audioPath = t.ref.path;
    t.title = title;
    t.artist = u"歌手"_s;
    t.album = u"專輯"_s;
    t.durationMs = 215600;
    return t;
}

struct StepFixture {
    bool app = (ensureApp(), true);
    FakeLrclib server;
    std::shared_ptr<LrclibSwitches> switches = std::make_shared<LrclibSwitches>();
    QStringList saveErrors;
    LrclibConfig config() const {
        LrclibConfig c;
        c.baseUrl = server.baseUrl();
        c.userAgent = lrclibUserAgent(u"0.5.0"_s);
        return c;
    }
    LrclibStep step{config(), switches, [this](const QString& r) {
                        saveErrors.append(r);
                    }};
};

} // namespace

TEST_CASE("AC-16-1：/api/get 帶曲名、藝人、專輯、四捨五入的秒數與 User-Agent") {
    StepFixture f;
    f.server.set(u"/api/get"_s, {200, kSyncedRecord});
    const auto doc = f.step.find(sampleTrack());
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Found);
    CHECK(doc->source == LyricsSource::Lrclib);
    CHECK(doc->synced);
    REQUIRE(doc->lines.size() == 2);
    CHECK(doc->lines[1].timeMs == 2500);
    REQUIRE(f.server.requests().size() == 1);
    const auto request = f.server.requests().first();
    // 「+」「&」與中日文都要百分比編碼（「+」不編會被當成空白）
    CHECK(request.target == u"/api/get?track_name=%E9%9B%A8%E3%81%AE%E6%97%A5%20%2B%20A%26B"
                            u"&artist_name=%E6%AD%8C%E6%89%8B&album_name=%E5%B0%88%E8%BC%AF"
                            u"&duration=216"_s);
    CHECK(request.userAgent == "AuralisPlayer/0.5.0 (https://github.com/rory12392/AuralisPlayer)");
}

TEST_CASE("AC-16-1：/api/get 404 時搜尋，取時長差 ≤ 3 秒且有同步歌詞的第一筆") {
    StepFixture f;
    f.server.set(u"/api/search"_s,
                 {200, R"([{"duration":209,"syncedLyrics":"[00:01.00]差太多"},)"
                       R"({"duration":214,"syncedLyrics":null,"plainLyrics":"沒有時間軸"},)"
                       R"({"duration":218.4,"syncedLyrics":"[00:03.00]這一筆"},)"
                       R"({"duration":215,"syncedLyrics":"[00:04.00]較後面"}])"});
    const auto doc = f.step.find(sampleTrack());
    REQUIRE(doc.has_value());
    REQUIRE(doc->lines.size() == 1);
    CHECK(doc->lines[0].text == u"這一筆"_s);
    REQUIRE(f.server.requests().size() == 2);
    CHECK(f.server.requests()[1].target.startsWith(u"/api/search?track_name="_s));
    CHECK_FALSE(f.server.requests()[1].target.contains(u"album_name"_s));
}

TEST_CASE("AC-16-1：兩個都沒有時這一段沒找到（交給來源鏈記 notfound）") {
    StepFixture f;
    f.server.set(u"/api/search"_s, {200, "[]"});
    CHECK_FALSE(f.step.find(sampleTrack()).has_value());
}

TEST_CASE("AC-16-2：伺服器 6 秒才回應時 5 秒放棄，回傳暫時取不到") {
    StepFixture f;
    CHECK(LrclibConfig().timeoutMs == 5000);
    f.server.set(u"/api/get"_s, {200, kSyncedRecord, 6000});
    QElapsedTimer timer;
    timer.start();
    const auto doc = f.step.find(sampleTrack());
    const qint64 elapsed = timer.elapsed();
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Unavailable);
    // 沒逾時的話 6 秒時會拿到伺服器的歌詞（上面的狀態就不是 Unavailable）；上限只擋卡住，
    // 留給平行建置時建立連線的額外時間
    CHECK(elapsed >= 4900);
    CHECK(elapsed < 8000);
}

TEST_CASE("AC-16-2：畸形 JSON、伺服器錯誤、超大回應、連不上都是暫時取不到") {
    StepFixture f;
    SECTION("畸形 JSON") {
        f.server.set(u"/api/get"_s, {200, "{\"syncedLyrics\": [00:01"});
    }
    SECTION("搜尋回傳不是陣列") {
        f.server.set(u"/api/search"_s, {200, "{\"oops\":1}"});
    }
    SECTION("500") {
        f.server.set(u"/api/get"_s, {500, "error"});
    }
    SECTION("超大回應") {
        f.server.set(u"/api/get"_s, {200, QByteArray(3 * 1024 * 1024, ' ') + kSyncedRecord});
    }
    SECTION("連不上") {
        LrclibConfig c = f.config();
        c.baseUrl = u"http://127.0.0.1:1"_s;
        LrclibStep offline(c, f.switches);
        const auto doc = offline.find(sampleTrack());
        REQUIRE(doc.has_value());
        CHECK(doc->status == LyricsDocument::Status::Unavailable);
        return;
    }
    const auto doc = f.step.find(sampleTrack());
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Unavailable);
}

TEST_CASE("AC-16-3：線上抓取關閉時這一段不開、不連線；查詢中關掉立刻中斷") {
    StepFixture f;
    f.server.set(u"/api/get"_s, {200, kSyncedRecord, 3000});
    f.switches->online = false;
    CHECK_FALSE(f.step.active());
    LyricsFinder finder;
    finder.append(std::make_unique<LrclibStep>(f.config(), f.switches));
    const auto none = finder.find(sampleTrack(), [] { return false; });
    REQUIRE(none.has_value());
    CHECK(none->status == LyricsDocument::Status::NotFound);
    CHECK(f.server.requests().isEmpty());

    // 開著查到一半改成關：50 ms 內 abort，不等 3 秒（等了就會拿到歌詞）；結果不是永久的 notfound。
    // 上限留給平行建置時建立連線的額外時間
    f.switches->online = true;
    QTimer::singleShot(200, [&] { f.switches->online = false; });
    QElapsedTimer timer;
    timer.start();
    const auto doc = f.step.find(sampleTrack());
    CHECK(timer.elapsed() < 2500);
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Unavailable);
}

TEST_CASE("AC-16-4：預設不另存；開啟時只在 .lrc 不存在時寫入，先寫暫存再改名") {
    StepFixture f;
    CHECK_FALSE(LrclibSwitches().saveLrc.load());
    f.server.set(u"/api/get"_s, {200, kSyncedRecord});
    const auto dir = lyrTempDir();
    LyricsTrack track = sampleTrack();
    track.ref.path = track.audioPath = dir->filePath(u"a.flac"_s);
    writeFile(track.audioPath, "audio");
    const QString lrc = dir->filePath(u"a.lrc"_s);

    REQUIRE(f.step.find(track).has_value());
    CHECK_FALSE(QFile::exists(lrc)); // 預設關閉

    f.switches->saveLrc = true;
    REQUIRE(f.step.find(track).has_value());
    REQUIRE(QFile::exists(lrc));
    CHECK_FALSE(QFile::exists(lrc + u".auralis-tmp"_s));
    QFile saved(lrc);
    REQUIRE(saved.open(QIODevice::ReadOnly));
    const LyricsDocument reread = parseLyrics(QString::fromUtf8(saved.readAll()));
    CHECK(reread.synced);
    CHECK(reread.lines.size() == 2);
    saved.close();
    // 音樂檔沒被動到
    QFile audio(track.audioPath);
    REQUIRE(audio.open(QIODevice::ReadOnly));
    CHECK(audio.readAll() == "audio");

    // 已經有 .lrc：不覆寫
    writeFile(lrc, "[00:00.00]使用者自己的");
    REQUIRE(f.step.find(track).has_value());
    QFile mine(lrc);
    REQUIRE(mine.open(QIODevice::ReadOnly));
    CHECK(mine.readAll() == "[00:00.00]使用者自己的");
    CHECK(f.saveErrors.isEmpty());
}

TEST_CASE("AC-16-4：寫不進去只回報原因，歌詞照樣回傳；CUE 曲目寫「主檔名 軌號.lrc」") {
    StepFixture f;
    f.switches->saveLrc = true;
    f.server.set(u"/api/get"_s, {200, kSyncedRecord});
    const auto dir = lyrTempDir();
    LyricsTrack missing = sampleTrack();
    missing.audioPath = dir->filePath(u"不存在的資料夾/a.flac"_s);
    const auto doc = f.step.find(missing);
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Found);
    CHECK(f.saveErrors.size() == 1);

    LyricsTrack cue = sampleTrack();
    cue.ref = {dir->filePath(u"Album.cue"_s), 3};
    cue.audioPath = dir->filePath(u"Album.flac"_s);
    cue.cueTrack = 3;
    const auto parsed = parseLrclibRecord(kSyncedRecord);
    REQUIRE(saveLrcBeside(cue, parsed).value());
    CHECK(QFile::exists(dir->filePath(u"Album 03.lrc"_s)));
    CHECK_FALSE(QFile::exists(dir->filePath(u"Album.lrc"_s)));
}

TEST_CASE("LYR 沒涵蓋的情況 1：曲名有版本後綴時查不到就去掉後綴再查") {
    CHECK(stripVersionSuffix(u"Song - 2025 Remaster"_s) == u"Song"_s);
    CHECK(stripVersionSuffix(u"Song (Instrumental)"_s) == u"Song"_s);
    CHECK(stripVersionSuffix(u"Song [Live] - Remastered 2011"_s) == u"Song"_s);
    CHECK(stripVersionSuffix(u"Song (feat. B)"_s) == u"Song (feat. B)"_s);
    CHECK(stripVersionSuffix(u"Live and Let Die"_s) == u"Live and Let Die"_s);
    CHECK(stripVersionSuffix(u"(Instrumental)"_s) == u"(Instrumental)"_s);

    StepFixture f;
    f.server.set(u"/api/search"_s, {200, "[]"});
    f.server.setFor(u"/api/get"_s, u"Song"_s, {200, kSyncedRecord});
    const auto doc = f.step.find(sampleTrack(u"Song - 2025 Remaster"_s));
    REQUIRE(doc.has_value());
    CHECK(doc->status == LyricsDocument::Status::Found);
    REQUIRE(f.server.requests().size() == 3);
    CHECK(f.server.requests()[2].target.startsWith(u"/api/get?track_name=Song&"_s));
}

TEST_CASE("LYR 沒涵蓋的情況 2：LRCLIB 回 instrumental 時是純音樂") {
    const LyricsDocument doc =
        parseLrclibRecord(R"({"instrumental":true,"plainLyrics":null,"syncedLyrics":null})");
    CHECK(doc.status == LyricsDocument::Status::Found);
    CHECK(doc.instrumental);
    CHECK(doc.lines.isEmpty());
    // 只有純文字時照樣用（沒有時間軸）
    const LyricsDocument plain = parseLrclibRecord(R"({"plainLyrics":"一\n二"})");
    CHECK(plain.status == LyricsDocument::Status::Found);
    CHECK_FALSE(plain.synced);
}
