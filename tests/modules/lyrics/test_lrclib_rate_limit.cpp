// LRCLIB 限流（T-99，US-16）：回 429／503 時照 Retry-After（秒數或 HTTP 日期；沒有或解析不了時
// 用預設 60 秒）等待，這段時間整個 LRCLIB 那一段不送任何請求、直接回傳暫時取不到，時間到後恢復；
// 不自動重試。全部對本機的假伺服器，不打真的 LRCLIB。
#include "fake_lrclib_server.h"
#include "lrclib.h"
#include "lyr_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QLocale>
#include <QThread>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

const QByteArray kRecord = R"({"duration":215,"syncedLyrics":"[00:01.00]第一行"})";

LyricsTrack track(const QString& title) {
    LyricsTrack t;
    t.ref = {u"C:\\music\\"_s + title + u".flac"_s, 0};
    t.audioPath = t.ref.path;
    t.title = title;
    t.artist = u"歌手"_s;
    t.album = u"專輯"_s;
    t.durationMs = 215000;
    return t;
}

/// HTTP 日期（RFC 9110 IMF-fixdate）：「Wed, 21 Oct 2015 07:28:00 GMT」。
QByteArray httpDate(const QDateTime& utc) {
    return QLocale::c().toString(utc.toUTC(), u"ddd, dd MMM yyyy HH:mm:ss 'GMT'"_s).toLatin1();
}

struct RateFixture {
    bool app = (ensureApp(), true);
    FakeLrclib server;
    std::shared_ptr<LrclibSwitches> switches = std::make_shared<LrclibSwitches>();
    LrclibStep step{config(), switches};
    LrclibConfig config() const {
        LrclibConfig c;
        c.baseUrl = server.baseUrl();
        c.userAgent = lrclibUserAgent(u"test"_s);
        return c;
    }
    LyricsDocument::Status find(const QString& title) {
        const auto doc = step.find(track(title));
        REQUIRE(doc.has_value());
        return doc->status;
    }
};

} // namespace

TEST_CASE("T-99：Retry-After 的秒數、HTTP 日期與預設") {
    const QDateTime now = QDateTime::fromString(u"2026-11-17T12:00:00Z"_s, Qt::ISODate);
    CHECK(kLrclibDefaultRetryAfterMs == 60000);
    CHECK(lrclibRetryAfterMs("2", now) == 2000);
    CHECK(lrclibRetryAfterMs(" 120 ", now) == 120000);
    CHECK(lrclibRetryAfterMs("0", now) == 0);
    CHECK(lrclibRetryAfterMs("Tue, 17 Nov 2026 12:01:30 GMT", now) == 90000);
    CHECK(lrclibRetryAfterMs("Tue, 17 Nov 2026 11:59:00 GMT", now) == 0); // 已經過了
    // 沒有標頭或解析不了：保守的預設
    CHECK(lrclibRetryAfterMs("", now) == kLrclibDefaultRetryAfterMs);
    CHECK(lrclibRetryAfterMs("soon", now) == kLrclibDefaultRetryAfterMs);
    CHECK(lrclibRetryAfterMs("-5", now) == kLrclibDefaultRetryAfterMs);
    CHECK(lrclibRetryAfterMs("1.5", now) == kLrclibDefaultRetryAfterMs);
}

TEST_CASE("T-99：429＋Retry-After: 2 → 2 秒內整段不送請求、換一首也一樣，之後恢復") {
    RateFixture f;
    f.server.set(u"/api/get"_s, {429, R"({"scope":"ip"})", 0, {{"Retry-After", "2"}}});
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.server.requests().size() == 1); // 沒有自動重試、也不接著搜尋
    CHECK(f.step.cooldownRemainingMs() > 1000);

    // 冷卻中：別首也不送（不是只停這一首）
    f.server.set(u"/api/get"_s, {200, kRecord});
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.server.requests().size() == 1);

    QThread::msleep(2200); // 這段時間不用處理假伺服器的事件
    CHECK(f.step.cooldownRemainingMs() == 0);
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Found);
    CHECK(f.server.requests().size() == 2);
}

TEST_CASE("T-99：Retry-After 是 HTTP 日期時等到那個時間") {
    RateFixture f;
    const QByteArray when = httpDate(QDateTime::currentDateTimeUtc().addSecs(30));
    f.server.set(u"/api/get"_s, {429, "", 0, {{"Retry-After", when}}});
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    // 日期只到秒：剩 29～30 秒
    CHECK(f.step.cooldownRemainingMs() > 28000);
    CHECK(f.step.cooldownRemainingMs() <= 30000);
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("T-99：429 沒有 Retry-After 時等預設 60 秒") {
    RateFixture f;
    f.server.set(u"/api/get"_s, {429, "Too Many Requests"});
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.step.cooldownRemainingMs() > 58000);
    CHECK(f.step.cooldownRemainingMs() <= kLrclibDefaultRetryAfterMs);
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("T-99：503（伺服器忙碌）同樣照 Retry-After 等；搜尋回 429 也算") {
    RateFixture f;
    SECTION("503") {
        f.server.set(u"/api/get"_s, {503, "", 0, {{"Retry-After", "1"}}});
    }
    SECTION("搜尋回 429") {
        f.server.set(u"/api/get"_s, {404, "{}"});
        f.server.set(u"/api/search"_s, {429, "", 0, {{"Retry-After", "1"}}});
    }
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    const qsizetype sent = f.server.requests().size();
    CHECK(f.step.cooldownRemainingMs() > 0);
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.server.requests().size() == sent);
}

TEST_CASE("T-99：其他錯誤（500）不冷卻，下一次照常查") {
    RateFixture f;
    f.server.set(u"/api/get"_s, {500, "error", 0, {{"Retry-After", "30"}}});
    CHECK(f.find(u"一"_s) == LyricsDocument::Status::Unavailable);
    CHECK(f.step.cooldownRemainingMs() == 0);
    f.server.set(u"/api/get"_s, {200, kRecord});
    CHECK(f.find(u"二"_s) == LyricsDocument::Status::Found);
}
