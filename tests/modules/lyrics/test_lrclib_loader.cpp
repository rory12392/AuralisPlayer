// LRCLIB 接在來源鏈最後（T-67）：經載入器、lyrics_cache 與單例 Lyrics 的整條路。
// 找到的寫快取、下次不再連線（AC-16-1）；逾時不寫成 notfound、「重試」再查（AC-16-2）；
// 關閉時記的 notfound 打開後重查、關閉時面板的說明（AC-16-3）；純音樂也記快取
// （沒涵蓋的情況 2）；快速換歌只查最後一首、舊查詢中斷（沒涵蓋的情況 3）。對本機的假伺服器。
#include "fake_lrclib_server.h"
#include "fake_playback_service.h"
#include "lrclib.h"
#include "lyr_test_helpers.h"
#include "lyrics_cache.h"
#include "lyrics_loader.h"
#include "lyrics_view.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

const QByteArray kSynced =
    R"({"duration":200,"syncedLyrics":"[00:01.00]線上第一行\n[00:02.00]線上第二行"})";

/// 一首在音樂庫裡、沒有本機歌詞的曲目，載入器的來源鏈最後接 LRCLIB（對假伺服器）。
struct OnlineFixture {
    bool app = (ensureApp(), true);
    FakeLrclib server;
    LyrDatabase db;
    std::unique_ptr<QTemporaryDir> dir = lyrTempDir();
    LyrLibrary library;
    JobRunner jobs;
    FakePlaybackService playback;
    std::shared_ptr<LrclibSwitches> switches = std::make_shared<LrclibSwitches>();
    LyricsLoader loader{jobs, library, *db.db};
    std::unique_ptr<LyricsView> view;

    OnlineFixture() {
        LrclibConfig config;
        config.baseUrl = server.baseUrl();
        config.userAgent = lrclibUserAgent(u"test"_s);
        config.timeoutMs = 800; // 逾時的 5 秒在 test_lrclib 量過，這裡縮短
        loader.appendSource(std::make_unique<LrclibStep>(config, switches));
        view = std::make_unique<LyricsView>(loader, playback, nullptr, switches);
    }
    TrackRef track(const QString& name, const QString& title = u"歌"_s) {
        const QString audio = dir->filePath(name);
        writeFile(audio, "audio");
        library.addTrack(audio, title, u"人"_s, u"輯"_s, 200000);
        return {audio, 0};
    }
    void loadAndWait(const TrackRef& ref) {
        loader.load(ref);
        REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    }
    std::optional<LyricsDocument> cached(const TrackRef& ref) {
        return LyricsCache(*db.db).read(resolveTrack(library, ref));
    }
};

} // namespace

TEST_CASE("AC-16-1：本機沒有歌詞時查 LRCLIB，存進快取，下次直接用") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    const TrackRef ref = f.track(u"a.flac"_s);
    f.loadAndWait(ref);
    CHECK(f.view->state() == u"synced"_s);
    CHECK(f.view->source() == u"lrclib"_s);
    CHECK(f.view->lines() == QStringList{u"線上第一行"_s, u"線上第二行"_s});
    CHECK(f.server.requests().size() == 1);
    const auto cached = f.cached(ref);
    REQUIRE(cached.has_value());
    CHECK(cached->source == LyricsSource::Lrclib);

    f.loader.clear();
    f.loadAndWait(ref);
    CHECK(f.view->source() == u"lrclib"_s);
    CHECK(f.server.requests().size() == 1); // 沒有再連線
}

TEST_CASE("AC-16-1：本機有歌詞時不連線") {
    OnlineFixture f;
    const TrackRef ref = f.track(u"b.flac"_s);
    f.library.setEmbedded(ref.path, u"[00:01.00]內嵌"_s);
    f.loadAndWait(ref);
    CHECK(f.view->source() == u"embedded"_s);
    CHECK(f.server.requests().isEmpty());
}

TEST_CASE("AC-15-1：內嵌暫時讀不到時顯示 LRCLIB 但不寫快取，內嵌恢復後換回內嵌") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    const TrackRef ref = f.track(u"locked.flac"_s);
    f.library.setEmbedded(ref.path, u"[00:01.00]內嵌"_s);
    f.library.failEmbedded(ref.path, 1); // 第一次讀的時候檔案被別的程式鎖住
    f.loadAndWait(ref);
    CHECK(f.view->source() == u"lrclib"_s); // 照常顯示後段找到的
    CHECK_FALSE(f.cached(ref).has_value());

    f.loader.clear();
    f.loadAndWait(ref); // 修改時間沒變
    CHECK(f.view->source() == u"embedded"_s);
    CHECK(f.view->lines() == QStringList{u"內嵌"_s});
}

TEST_CASE("AC-16-4：另存 .lrc 開著、內嵌暫時讀不到時，LRCLIB 的結果照常顯示但不另存 .lrc") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    f.view->setSaveLrc(true);
    const TrackRef ref = f.track(u"locked.flac"_s);
    f.library.setEmbedded(ref.path, u"[00:01.00]內嵌"_s);
    f.library.failEmbedded(ref.path, 1); // 第一次讀的時候檔案被別的程式鎖住
    f.loadAndWait(ref);
    CHECK(f.view->source() == u"lrclib"_s);
    // 存了的話 .lrc 在來源鏈第一段，內嵌恢復後也換不回來
    CHECK(QDir(f.dir->path()).entryList({u"*.lrc"_s, u"*.auralis-tmp"_s}, QDir::Files).isEmpty());

    f.loader.clear();
    f.loadAndWait(ref); // 修改時間沒變
    CHECK(f.view->source() == u"embedded"_s);
    CHECK(f.view->lines() == QStringList{u"內嵌"_s});
}

TEST_CASE("AC-16-4：另存 .lrc 開著、確定沒有內嵌歌詞時照舊另存") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    f.view->setSaveLrc(true);
    const TrackRef ref = f.track(u"plain.flac"_s);
    f.loadAndWait(ref);
    CHECK(f.view->source() == u"lrclib"_s);
    const QString lrc = f.dir->filePath(u"plain.lrc"_s);
    REQUIRE(QFile::exists(lrc));
    QFile saved(lrc);
    REQUIRE(saved.open(QIODevice::ReadOnly));
    CHECK(parseLyrics(QString::fromUtf8(saved.readAll())).lines.size() == 2);
}

TEST_CASE("AC-16-2：沒有回應時面板是「暫時無法取得」、不記 notfound，重試會再查") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced, 3000});
    const TrackRef ref = f.track(u"c.flac"_s);
    f.loadAndWait(ref);
    CHECK(f.view->state() == u"unavailable"_s);
    CHECK_FALSE(f.cached(ref).has_value());

    f.server.set(u"/api/get"_s, {200, kSynced});
    f.view->retry();
    REQUIRE(waitFor([&] { return f.view->state() == u"synced"_s; }));
    CHECK(f.view->source() == u"lrclib"_s);
}

TEST_CASE("T-99：被 LRCLIB 限流時面板是「暫時無法取得」，冷卻中換歌與重試都不連線") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {429, "", 0, {{"Retry-After", "30"}}});
    const TrackRef first = f.track(u"limited.flac"_s);
    f.loadAndWait(first);
    CHECK(f.view->state() == u"unavailable"_s);
    CHECK_FALSE(f.cached(first).has_value()); // 不記成 notfound
    CHECK(f.server.requests().size() == 1);

    f.server.set(u"/api/get"_s, {200, kSynced});
    const TrackRef second = f.track(u"next.flac"_s, u"下一首"_s);
    f.loadAndWait(second);
    CHECK(f.view->state() == u"unavailable"_s);
    f.view->retry();
    REQUIRE(waitFor([&] { return !f.loader.isLoading(); }));
    CHECK(f.view->state() == u"unavailable"_s);
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("AC-16-3：關閉時「沒有歌詞」帶說明；打開後關閉時記的 notfound 會重查") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    const TrackRef ref = f.track(u"d.flac"_s);
    f.view->setOnlineEnabled(false);
    REQUIRE(waitFor([&] { return !f.loader.isLoading(); }));
    f.loadAndWait(ref);
    CHECK(f.view->state() == u"none"_s);
    CHECK_FALSE(f.view->onlineEnabled());
    CHECK(f.server.requests().isEmpty());
    const auto offline = f.cached(ref);
    REQUIRE(offline.has_value());
    CHECK(offline->source == LyricsSource::Embedded); // 記的是當時最後一段

    f.view->setOnlineEnabled(true); // 立刻生效：目前這首重找
    REQUIRE(waitFor([&] { return f.view->state() == u"synced"_s; }));
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("AC-16-4：另存 .lrc 的設定預設關閉，設定頁改了交給 LRCLIB 那段") {
    OnlineFixture f;
    CHECK_FALSE(f.view->saveLrc());
    CHECK(f.view->onlineEnabled());
    f.view->setSaveLrc(true);
    CHECK(f.switches->saveLrc.load());
    CHECK(f.view->saveLrc());
}

TEST_CASE("LYR 沒涵蓋的情況 2：純音樂顯示「純音樂」，快取也記住") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, R"({"instrumental":true})"});
    const TrackRef ref = f.track(u"e.flac"_s);
    f.loadAndWait(ref);
    CHECK(f.view->state() == u"instrumental"_s);
    const auto cached = f.cached(ref);
    REQUIRE(cached.has_value());
    CHECK(cached->instrumental);
    CHECK(cached->status == LyricsDocument::Status::Found);

    f.loader.clear();
    f.loadAndWait(ref);
    CHECK(f.view->state() == u"instrumental"_s);
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("LYR 沒涵蓋的情況 3：快速換歌只用最後一首的結果，舊查詢中斷、不寫快取") {
    OnlineFixture f;
    f.server.set(u"/api/get"_s, {200, kSynced});
    f.server.setFor(u"/api/get"_s, u"%E8%88%8A"_s,
                    {200, R"({"syncedLyrics":"[00:01.00]舊的那首"})", 2000});
    const TrackRef old = f.track(u"old.flac"_s, u"舊"_s);
    const TrackRef latest = f.track(u"new.flac"_s, u"新"_s);
    f.loader.load(old);
    REQUIRE(waitFor([&] { return f.server.requests().size() == 1; }));
    f.loadAndWait(latest);
    CHECK(f.view->lines().value(0) == u"線上第一行"_s);
    REQUIRE(waitFor([&] { return f.server.abortedBeforeReply() == 1; }, 1000));
    waitFor([] { return false; }, 2200); // 舊的回應時間過了也不會蓋過去
    CHECK(f.loader.track().path == latest.path);
    CHECK(f.view->lines().value(0) == u"線上第一行"_s);
    CHECK_FALSE(f.cached(old).has_value());
}

// P-LYR-10：模組停止後核心才依序解構音樂庫、資料庫，最後才是 JobRunner，所以停止時要等自己的工作。
// 「解構」用 LyrLibrary::gone 替身旗標表示：shutdown 返回後立刻設，之後被查詢就是解構後用到。
namespace {

/// 包住 LRCLIB：查完（含被 abort）之後再查一次音樂庫，代表工作收尾時還會用到核心服務
/// （真正的工作在查完後寫快取）。工作結束前 shutdown 就返回的話，這次查詢會落在「解構後」。
class TailStep final : public LyricsStep {
public:
    TailStep(std::unique_ptr<LyricsStep> inner, const LyrLibrary& library)
        : m_inner(std::move(inner)), m_library(library) {}
    LyricsSource kind() const override { return m_inner->kind(); }
    bool active() const override { return m_inner->active(); }
    std::optional<LyricsDocument> find(const LyricsTrack& track) override {
        return find(track, [] { return false; }, false);
    }
    std::optional<LyricsDocument> find(const LyricsTrack& track,
                                       const std::function<bool()>& cancelled) override {
        return find(track, cancelled, false);
    }
    std::optional<LyricsDocument> find(const LyricsTrack& track,
                                       const std::function<bool()>& cancelled,
                                       bool earlierFailed) override {
        auto doc = m_inner->find(track, cancelled, earlierFailed);
        (void)m_library.audioFilePath({track.audioPath, 0});
        return doc;
    }

private:
    std::unique_ptr<LyricsStep> m_inner;
    const LyrLibrary& m_library;
};

} // namespace

TEST_CASE("AC-16-2（P-LYR-10）：停止時中斷進行中的 LRCLIB 查詢並等工作結束，幾百毫秒內返回") {
    ensureApp();
    FakeLrclib server;
    server.set(u"/api/get"_s, {200, kSynced, 3000}); // 回應很慢，停止時查詢一定還在進行
    LyrDatabase db;
    std::unique_ptr<QTemporaryDir> dir = lyrTempDir();
    LyrLibrary library;
    JobRunner jobs;
    LyricsLoader loader{jobs, library, *db.db};
    LrclibConfig config;
    config.baseUrl = server.baseUrl();
    config.userAgent = lrclibUserAgent(u"test"_s);
    loader.appendSource(std::make_unique<TailStep>(
        std::make_unique<LrclibStep>(config, std::make_shared<LrclibSwitches>()), library));
    const QString audio = dir->filePath(u"slow.flac"_s);
    writeFile(audio, "audio");
    library.addTrack(audio, u"歌"_s, u"人"_s, u"輯"_s, 200000);
    loader.load({audio, 0});
    REQUIRE(waitFor([&] { return server.requests().size() == 1; }));
    QElapsedTimer elapsed;
    elapsed.start();
    loader.shutdown(7000);
    const qint64 ms = elapsed.elapsed();
    library.gone = true;
    CHECK(ms < 500); // abort 生效，沒有等滿逾時（5 秒）或等回應（3 秒）
    REQUIRE(jobs.waitForDone(10000));
    CHECK(library.usedAfterGone.load() == 0); // 收尾的查詢發生在 shutdown 返回之前
}

TEST_CASE("AC-16-2（P-LYR-10）：停止時還在排隊的查詢之後才開始也不碰音樂庫，停止立刻返回") {
    ensureApp();
    LyrDatabase db;
    LyrLibrary library;
    JobRunner jobs(1); // 一條執行緒：先佔住，查詢就只能排隊
    LyricsLoader loader{jobs, library, *db.db};
    std::atomic<bool> release{false};
    jobs.submit(u"佔住執行緒"_s, [&release](JobContext&) {
        while (!release.load()) {
            QThread::msleep(1);
        }
    });
    loader.load({u"C:/lyr-p10/queued.flac"_s, 0});
    QElapsedTimer elapsed;
    elapsed.start();
    loader.shutdown(7000);
    CHECK(elapsed.elapsed() < 500); // 沒開始的工作不用等
    library.gone = true;
    release = true;
    REQUIRE(jobs.waitForDone(10000));
    CHECK(library.usedAfterGone.load() == 0);
}
