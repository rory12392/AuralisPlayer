// .cue 解析：時間換算、區段（INDEX 01 到下一軌的 INDEX 01）、多個 FILE、錯誤（AC-07-1、AC-07-2）。
#include "cue_fixtures.h"

#include "library/cue_sheet.h"

#include <auralis/text_decoder.h>

#include <catch2/catch_test_macros.hpp>

using auralis::TextDecoder;
using namespace auralis::core;
using namespace auralis::test;

TEST_CASE("AC-07-1：解析樣本 .cue 的專輯、每軌曲名、藝人與順序") {
    const auto sheet =
        parseCueSheet(TextDecoder::decode(cueFixture(QStringLiteral("utf8-bom.cue"))).text);
    REQUIRE(sheet.hasValue());
    const CueSheet& cue = sheet.value();
    CHECK(cue.title == QStringLiteral("雙語測試專輯：夜と朝"));
    CHECK(cue.performer == QStringLiteral("測試樂團"));
    CHECK(cue.rem.value(QStringLiteral("GENRE")) == QStringLiteral("Soundtrack"));
    CHECK(cue.rem.value(QStringLiteral("DATE")) == QStringLiteral("2019"));
    REQUIRE(cue.files.size() == 1);
    CHECK(cue.files[0] == QStringLiteral("album.flac"));
    REQUIRE(cue.tracks.size() == 3);
    CHECK(cue.tracks[0].number == 1);
    CHECK(cue.tracks[0].title == QStringLiteral("序曲：開始"));
    CHECK(cue.tracks[1].title == QStringLiteral("夜明けの街を歩く"));
    CHECK(cue.tracks[1].performer == QStringLiteral("歌手 A"));
    CHECK(cue.tracks[2].title == QStringLiteral("終章 Ｅｎｄｉｎｇ"));
    CHECK(cue.tracks[2].performer.isEmpty());
}

TEST_CASE("AC-07-2：區段是 INDEX 01 到下一軌的 INDEX 01，前置空白算在上一軌") {
    const auto sheet =
        parseCueSheet(TextDecoder::decode(cueFixture(QStringLiteral("utf8-bom.cue"))).text);
    REQUIRE(sheet.hasValue());
    const auto& tracks = sheet.value().tracks;
    // 00:00:00、00:02:00（INDEX 00 在 00:01:50）、00:03:37
    CHECK(tracks[0].start == kSampleTrackStart[0]);
    CHECK(tracks[0].end == kSampleTrackStart[1]);
    CHECK(tracks[1].start == kSampleTrackStart[1]);
    CHECK(tracks[1].end == kSampleTrackStart[2]);
    CHECK(tracks[2].start == kSampleTrackStart[2]);
    CHECK(tracks[2].end == -1);
}

TEST_CASE("AC-07-2：時間換算、CRLF、沒有引號的值、多個 FILE、跳過非音訊軌") {
    const QString text = QStringLiteral("PERFORMER Someone Else\r\n"
                                        "TITLE Long Live\r\n"
                                        "FILE \"disc 1.wav\" WAVE\r\n"
                                        "  TRACK 1 AUDIO\r\n"
                                        "    INDEX 01 00:00:00\r\n"
                                        "  TRACK 2 AUDIO\r\n"
                                        "    TITLE \"Second\"\r\n"
                                        "    INDEX 01 74:59:74\r\n"
                                        "FILE \"disc 2.flac\" WAVE\r\n"
                                        "  TRACK 3 MODE1/2352\r\n"
                                        "    TITLE \"Data\"\r\n"
                                        "    INDEX 01 00:00:00\r\n"
                                        "  TRACK 4 AUDIO\r\n"
                                        "    INDEX 01 100:00:01\r\n");
    const auto sheet = parseCueSheet(text);
    REQUIRE(sheet.hasValue());
    const CueSheet& cue = sheet.value();
    CHECK(cue.performer == QStringLiteral("Someone Else"));
    CHECK(cue.title == QStringLiteral("Long Live"));
    REQUIRE(cue.files.size() == 2);
    CHECK(cue.files[0] == QStringLiteral("disc 1.wav"));
    REQUIRE(cue.tracks.size() == 3); // 資料軌不收
    CHECK(cue.tracks[0].end == (74 * 60 + 59) * 75 + 74);
    CHECK(cue.tracks[1].start == (74 * 60 + 59) * 75 + 74);
    CHECK(cue.tracks[1].end == -1); // 下一軌在另一個 FILE：播到這個檔案結尾
    CHECK(cue.tracks[1].title == QStringLiteral("Second"));
    CHECK(cue.tracks[2].number == 4);
    CHECK(cue.tracks[2].fileIndex == 1);
    CHECK(cue.tracks[2].start == 100 * 60 * 75 + 1);
}

TEST_CASE("AC-07-2：一軌一檔、間隔接在上一個 FILE 結尾的 .cue：每軌對應自己的 FILE、播到檔尾") {
    // 一軌一檔的抓軌方式：下一軌的 TRACK 與 INDEX 00 寫在上一個 FILE 裡，INDEX 01 才在新的 FILE
    const QString text = QStringLiteral("FILE \"01.wav\" WAVE\n"
                                        "  TRACK 01 AUDIO\n"
                                        "    TITLE \"一\"\n"
                                        "    INDEX 01 00:00:00\n"
                                        "  TRACK 02 AUDIO\n"
                                        "    TITLE \"二\"\n"
                                        "    INDEX 00 00:03:10\n"
                                        "FILE \"02.wav\" WAVE\n"
                                        "    INDEX 01 00:00:00\n"
                                        "  TRACK 03 AUDIO\n"
                                        "    INDEX 00 00:02:00\n"
                                        "FILE \"03.wav\" WAVE\n"
                                        "    INDEX 01 00:00:00\n");
    const auto sheet = parseCueSheet(text);
    REQUIRE(sheet.hasValue());
    const auto& tracks = sheet.value().tracks;
    REQUIRE(sheet.value().files.size() == 3);
    REQUIRE(tracks.size() == 3);
    for (size_t i = 0; i < tracks.size(); ++i) {
        CAPTURE(i);
        CHECK(tracks[i].number == static_cast<int>(i) + 1);
        CHECK(tracks[i].fileIndex == static_cast<int>(i));
        CHECK(tracks[i].start == 0);
        CHECK(tracks[i].end == -1); // 上一個檔案結尾的間隔算在上一軌（相鄰兩軌接起來是連續的）
    }
    CHECK(tracks[1].title == QStringLiteral("二"));
}

TEST_CASE("解析失敗：沒有音訊軌、時間格式錯誤、缺 INDEX 01、TRACK 前沒有 FILE、軌號重複") {
    const char* broken[] = {
        "TITLE \"x\"\n",
        "FILE \"a.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:61:00\n",
        "FILE \"a.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 00 00:00:00\n",
        "TRACK 01 AUDIO\n INDEX 01 00:00:00\n",
        "FILE \"a.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n TRACK 01 AUDIO\n"
        " INDEX 01 00:01:00\n",
        "FILE \"a.wav\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:02:00\n TRACK 02 AUDIO\n"
        " INDEX 01 00:01:00\n",
    };
    for (const char* text : broken) {
        CAPTURE(text);
        const auto sheet = parseCueSheet(QString::fromUtf8(text));
        REQUIRE_FALSE(sheet.hasValue());
        CHECK_FALSE(sheet.error().message.isEmpty());
    }
}
