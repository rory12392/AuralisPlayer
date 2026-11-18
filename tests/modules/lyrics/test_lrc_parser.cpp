// LRC 解析（AC-15-4、模組規格 §1 的 LRC 解析規則與沒涵蓋的情況 5）、位置 → 行號。
#include "lyr_test_helpers.h"
#include "lyrics_document.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

QList<qint64> times(const LyricsDocument& doc) {
    QList<qint64> out;
    for (const LyricLine& l : doc.lines) {
        out.append(l.timeMs);
    }
    return out;
}

QStringList texts(const LyricsDocument& doc) {
    QStringList out;
    for (const LyricLine& l : doc.lines) {
        out.append(l.text);
    }
    return out;
}

} // namespace

TEST_CASE("AC-15-4：[mm:ss.xx] 與 [mm:ss.xxx] 都解析成毫秒，標頭整行忽略") {
    const LyricsDocument doc = parseLyrics(
        QStringLiteral("[ti:歌名]\r\n[ar:藝人]\r\n[al:專輯]\r\n[by:做的人]\r\n[length:03:20]\r\n"
                       "[00:01.50]第一行\r\n[01:02.345]第二行\r\n[10:00.07]第三行\r\n"));
    CHECK(doc.status == LyricsDocument::Status::Found);
    CHECK(doc.synced);
    CHECK(times(doc) == QList<qint64>{1500, 62'345, 600'070});
    CHECK(texts(doc) == QStringList{u"第一行"_s, u"第二行"_s, u"第三行"_s});
}

TEST_CASE("AC-15-4：一行多個時間標籤，每個時間各一行") {
    const LyricsDocument doc =
        parseLyrics(QStringLiteral("[00:10.00][00:30.00][00:20.00]副歌\n[00:15.00]主歌\n"));
    CHECK(times(doc) == QList<qint64>{10'000, 15'000, 20'000, 30'000});
    CHECK(texts(doc) == QStringList{u"副歌"_s, u"主歌"_s, u"副歌"_s, u"副歌"_s});
}

TEST_CASE("AC-15-4：[offset:] 正值讓歌詞提前、負值延後，不會小於 0") {
    const QString body = QStringLiteral("[00:00.20]零\n[00:10.00]十\n");
    CHECK(times(parseLyrics(u"[offset:+500]\n"_s + body)) == QList<qint64>{0, 9500});
    CHECK(times(parseLyrics(u"[offset:-500]\n"_s + body)) == QList<qint64>{700, 10'500});
    // offset 寫在檔尾也套用；寫壞的 offset 不套用
    CHECK(times(parseLyrics(body + u"[offset:1000]"_s)) == QList<qint64>{0, 9000});
    CHECK(times(parseLyrics(u"[offset:abc]\n"_s + body)) == QList<qint64>{200, 10'000});
}

TEST_CASE("規格 §1 情況 5：時間不遞增或重複時依時間排序，同時間保留原順序") {
    const LyricsDocument doc = parseLyrics(QString::fromUtf8(lyrSample(u"malformed.lrc"_s)));
    CHECK(doc.synced);
    CHECK(times(doc) == QList<qint64>{1000, 3000, 3000, 7000, 9500, 11'000});
    CHECK(texts(doc) == QStringList{u"第一行與第四行"_s, u"第二行（時間比下一行晚）"_s,
                                    u"第三行（和第二行同時間，保留原順序）"_s, u"第一行與第四行"_s,
                                    u"一位小數"_s, u"沒有小數"_s});
}

TEST_CASE("AC-15-1：沒有時間標籤是純文字，段落空行保留、頭尾空行去掉") {
    const LyricsDocument doc =
        parseLyrics(QStringLiteral("\n\n[Chorus]\n第一段\n\n第二段 [不是時間]\n\n"));
    CHECK(doc.status == LyricsDocument::Status::Found);
    CHECK_FALSE(doc.synced);
    CHECK(texts(doc) == QStringList{u"[Chorus]"_s, u"第一段"_s, QString(), u"第二段 [不是時間]"_s});
    CHECK(times(doc) == QList<qint64>{-1, -1, -1, -1});
    CHECK(lineIndexAt(doc, 5000) == -1);
}

TEST_CASE("AC-15-1：只有標頭或空白的歌詞算沒找到") {
    CHECK(parseLyrics(QString()).status == LyricsDocument::Status::NotFound);
    CHECK(parseLyrics(QStringLiteral("[ti:x]\n[ar:y]\n  \n")).status ==
          LyricsDocument::Status::NotFound);
}

TEST_CASE("提供給 T-66：位置 → 行號用二分搜尋，第一行之前是 -1") {
    const LyricsDocument doc = parseLyrics(
        QStringLiteral("[offset:500]\n[00:01.00]a\n[00:02.00]b\n[00:02.00]c\n[00:04.00]d\n"));
    CHECK(lineIndexAt(doc, 0) == -1);
    CHECK(lineIndexAt(doc, 499) == -1);
    CHECK(lineIndexAt(doc, 500) == 0); // offset 已套用：01.00 提前 500 ms
    CHECK(lineIndexAt(doc, 1499) == 0);
    CHECK(lineIndexAt(doc, 1500) == 2); // 同時間的兩行停在後一行
    CHECK(lineIndexAt(doc, 3499) == 2);
    CHECK(lineIndexAt(doc, 3500) == 3);
    CHECK(lineIndexAt(doc, 999'999) == 3);
    CHECK(lineIndexAt(LyricsDocument(), 1000) == -1);
}

TEST_CASE("快取的文字解析回來和原本相同（同步與純文字）") {
    for (const QString& text :
         {QString::fromUtf8(lyrSample(u"zh-source.lrc"_s)),
          QString::fromUtf8(lyrSample(u"malformed.lrc"_s)), u"純文字\n\n第二段"_s}) {
        const LyricsDocument doc = parseLyrics(text);
        const LyricsDocument back = parseLyrics(serializeLyrics(doc));
        CHECK(back.synced == doc.synced);
        CHECK(back.lines == doc.lines);
    }
}

TEST_CASE("CUE 借用整軌 .lrc：時間換成曲目內的位置，只留區段內的行") {
    const LyricsDocument doc =
        parseLyrics(QStringLiteral("[00:01.00]一\n[00:02.00]二\n[00:03.00]三\n[00:04.00]四\n"));
    const LyricsDocument mid = sliceLyrics(doc, 2000, 4000);
    CHECK(times(mid) == QList<qint64>{0, 1000});
    CHECK(texts(mid) == QStringList{u"二"_s, u"三"_s});
    CHECK(times(sliceLyrics(doc, 3000, -1)) == QList<qint64>{0, 1000});
    CHECK(sliceLyrics(doc, 10'000, -1).status == LyricsDocument::Status::NotFound);
}

TEST_CASE("風險：畸形與巨大的輸入不會當掉或卡住") {
    using Clock = std::chrono::steady_clock;
    // 一行 8 MB、沒有換行；20 萬行、每行 5 個時間標籤；大量沒關的括號
    const QString hugeLine(8 * 1024 * 1024, QLatin1Char('x'));
    QString manyTags;
    for (int i = 0; i < 200'000; ++i) {
        manyTags +=
            QStringLiteral("[%1:00.00][00:01][00:02.5][00:03.25][00:04.125]行\n").arg(i % 1000);
    }
    const QString brackets = QString(1024 * 1024, QLatin1Char('['));
    const QString binary = QString::fromLatin1(QByteArray(1024 * 1024, '\0'));

    const auto start = Clock::now();
    CHECK(parseLyrics(hugeLine).lines.size() == 1);
    CHECK(parseLyrics(manyTags).lines.size() == 1'000'000);
    CHECK(parseLyrics(brackets).lines.size() == 1);
    CHECK(parseLyrics(binary).status == LyricsDocument::Status::Found);
    CHECK(Clock::now() - start < std::chrono::seconds(10));
}
