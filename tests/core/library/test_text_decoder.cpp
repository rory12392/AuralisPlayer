// 文字解碼（AC-07-3）：BOM、嚴格 UTF-8、uchardet（Shift-JIS、Big5）、系統 ANSI；讀檔唯讀。
#include "cue_fixtures.h"
#include "library_fixtures.h"

#include <auralis/text_decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QStringDecoder>

using auralis::DecodedText;
using auralis::TextDecoder;
using namespace auralis::test;

TEST_CASE("AC-07-3：UTF-8 含 BOM 的 .cue 解出原文，BOM 不留在文字裡") {
    const QByteArray bytes = cueFixture(QStringLiteral("utf8-bom.cue"));
    REQUIRE(bytes.startsWith("\xEF\xBB\xBF"));
    const DecodedText decoded = TextDecoder::decode(bytes);
    CHECK(decoded.encoding == QStringLiteral("UTF-8"));
    CHECK(decoded.hadBom);
    CHECK(decoded.text == QString::fromUtf8(bytes.mid(3)));
    CHECK(decoded.text.contains(QStringLiteral("夜明けの街を歩く")));
    CHECK_FALSE(decoded.text.contains(QChar(0xFEFF)));
}

TEST_CASE("AC-07-3：Shift-JIS 與 Big5 的 .cue 由 uchardet 判斷編碼，解出原文") {
    struct Case {
        const char* fixture;
        unsigned codePage;
        const char* charset;
    };
    for (const Case c :
         {Case{"ja-source.cue", 932, "SHIFT_JIS"}, Case{"zh-source.cue", 950, "BIG5"}}) {
        CAPTURE(c.fixture);
        const QByteArray utf8 = cueFixture(QString::fromLatin1(c.fixture));
        const QByteArray encoded = encodeCodePage(utf8, c.codePage);
        REQUIRE(encoded != utf8); // 真的有非 ASCII 字元
        const DecodedText decoded = TextDecoder::decode(encoded);
        CHECK(decoded.encoding.toUpper() == QLatin1String(c.charset));
        CHECK_FALSE(decoded.hadBom);
        CHECK(decoded.text == QString::fromUtf8(utf8));
    }
}

TEST_CASE("AC-07-3：沒有 BOM 的 UTF-8、純 ASCII、UTF-16 BOM") {
    const QString text = QStringLiteral("TITLE \"夜明けの街\"\r\nPERFORMER \"臺北\"\r\n");
    const DecodedText utf8 = TextDecoder::decode(text.toUtf8());
    CHECK(utf8.encoding == QStringLiteral("UTF-8"));
    CHECK_FALSE(utf8.hadBom);
    CHECK(utf8.text == text); // 換行原樣保留

    const DecodedText ascii = TextDecoder::decode(QByteArrayLiteral("FILE \"a.flac\" WAVE\n"));
    CHECK(ascii.encoding == QStringLiteral("UTF-8"));
    CHECK(ascii.text == QStringLiteral("FILE \"a.flac\" WAVE\n"));

    QStringEncoder encoder(QStringConverter::Utf16LE);
    const QByteArray utf16 = QByteArray("\xFF\xFE", 2) + QByteArray(encoder.encode(text));
    const DecodedText decoded16 = TextDecoder::decode(utf16);
    CHECK(decoded16.encoding == QStringLiteral("UTF-16LE"));
    CHECK(decoded16.hadBom);
    CHECK(decoded16.text == text);

    CHECK(TextDecoder::decode({}).text.isEmpty());
}

TEST_CASE("TextDecoder::decodeFile 唯讀讀檔；檔案不存在時回傳錯誤") {
    const QString dir = freshTestDir(QStringLiteral("t-11-decoder"));
    const QString path = dir + QStringLiteral("\\長い名前の歌詞 ファイル.cue");
    const QByteArray bytes = encodeCodePage(cueFixture(QStringLiteral("ja-source.cue")), 932);
    writeBytes(path, bytes);
    const auto before = QFileInfo(path).lastModified();

    const auto decoded = TextDecoder::decodeFile(path);
    REQUIRE(decoded.hasValue());
    CHECK(decoded.value().text.contains(QStringLiteral("遠い記憶／静かな港町")));
    CHECK(QFileInfo(path).lastModified() == before);
    CHECK(QFileInfo(path).size() == bytes.size());
    // Qt 風格的 / 分隔符也讀得到（長路徑前綴不會自動轉換）
    CHECK(TextDecoder::decodeFile(QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')))
              .hasValue());

    const auto missing = TextDecoder::decodeFile(dir + QStringLiteral("\\沒有這個檔.cue"));
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().call == QStringLiteral("CreateFileW"));
}
