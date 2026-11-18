// 標籤讀取（TagLib，含 DSF、DSDIFF）、路徑比對鍵與搜尋正規化。
#include "library/tag_reader.h"
#include "library_fixtures.h"

#include <auralis/library_query.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QFile>

#include <windows.h>

using auralis::core::readEmbeddedLyrics;
using auralis::core::readTags;
using namespace auralis::test;

namespace {

const Tags kTags{{QStringLiteral("TITLE"), QStringLiteral("ＮｉｅＲ：Automata 遺されたもの")},
                 {QStringLiteral("ARTIST"), QStringLiteral("岡部啓一")},
                 {QStringLiteral("ALBUMARTIST"), QStringLiteral("Various")},
                 {QStringLiteral("ALBUM"), QStringLiteral("NieR Original Soundtrack")},
                 {QStringLiteral("COMPOSER"), QStringLiteral("Keiichi Okabe")},
                 {QStringLiteral("DATE"), QStringLiteral("2017-03-29")},
                 {QStringLiteral("TRACKNUMBER"), QStringLiteral("3/12")},
                 {QStringLiteral("DISCNUMBER"), QStringLiteral("2")},
                 {QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringLiteral("-6.50 dB")},
                 {QStringLiteral("LYRICS"), QStringLiteral("第一行\n第二行")}};

void requireCommonTags(const auralis::core::TagData& tags) {
    CHECK(tags.title == kTags[QStringLiteral("TITLE")]);
    CHECK(tags.artist == kTags[QStringLiteral("ARTIST")]);
    CHECK(tags.albumArtist == QStringLiteral("Various"));
    CHECK(tags.album == QStringLiteral("NieR Original Soundtrack"));
    CHECK(tags.year == 2017);
    CHECK(tags.trackNo == 3);
}

} // namespace

TEST_CASE("AC-08-2：FLAC 的標籤與音訊屬性（曲目資料來源）") {
    const QString path = freshTestDir(QStringLiteral("tags")) + QStringLiteral("\\曲目.flac");
    writeFlac(path, kTags, 44100, 16, 22050);
    const auto tags = readTags(path);
    REQUIRE(tags.hasValue());
    requireCommonTags(tags.value());
    CHECK(tags.value().composer == QStringLiteral("Keiichi Okabe"));
    CHECK(tags.value().discNo == 2);
    CHECK(tags.value().codec == QStringLiteral("FLAC"));
    CHECK(tags.value().sampleRate == 44100);
    CHECK(tags.value().bits == 16);
    CHECK(tags.value().channels == 2);
    CHECK(tags.value().durationMs == 500);
    CHECK(tags.value().dsdRate == 0);
    REQUIRE(tags.value().rgTrackGain.has_value());
    CHECK_THAT(*tags.value().rgTrackGain, Catch::Matchers::WithinAbs(-6.5, 1e-9));
    CHECK_FALSE(tags.value().rgAlbumGain.has_value());
    CHECK(tags.value().hasEmbeddedLyrics);
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("第一行\n第二行"));
}

TEST_CASE("AC-08-2：WAV 96 kHz／24 位元的標籤與位元深度") {
    const QString path = freshTestDir(QStringLiteral("tags-wav")) + QStringLiteral("\\a.wav");
    writeWav(path, kTags);
    const auto tags = readTags(path);
    REQUIRE(tags.hasValue());
    requireCommonTags(tags.value());
    CHECK(tags.value().codec == QStringLiteral("WAV"));
    CHECK(tags.value().sampleRate == 96000);
    CHECK(tags.value().bits == 24);
}

TEST_CASE("AC-08-2：DSF 的標籤、DSD 率與時長（TagLib 的 DSF 支援）") {
    const QString path = freshTestDir(QStringLiteral("tags-dsf")) + QStringLiteral("\\a.dsf");
    writeDsf(path, kTags);
    const auto tags = readTags(path);
    REQUIRE(tags.hasValue());
    requireCommonTags(tags.value());
    CHECK(tags.value().codec == QStringLiteral("DSD"));
    CHECK(tags.value().dsdRate == 2822400);
    CHECK(tags.value().sampleRate == 2822400);
    CHECK(tags.value().bits == 1);
    CHECK(tags.value().channels == 2);
    CHECK(tags.value().durationMs == 500);
}

TEST_CASE("AC-08-2：DSDIFF（.dff）的標籤、DSD 率與時長（TagLib 的 DSDIFF 支援）") {
    const QString path = freshTestDir(QStringLiteral("tags-dff")) + QStringLiteral("\\a.dff");
    writeDff(path, kTags);
    const auto tags = readTags(path);
    REQUIRE(tags.hasValue());
    requireCommonTags(tags.value());
    CHECK(tags.value().codec == QStringLiteral("DSD"));
    CHECK(tags.value().dsdRate == 2822400);
    CHECK(tags.value().channels == 2);
    CHECK(tags.value().durationMs == 500);
}

TEST_CASE("AC-08-2：MP3 沒有標籤時格式照樣讀得到") {
    const auto tags =
        readTags(QString::fromUtf8(AURALIS_FIXTURES) + QStringLiteral("/audio/sine-440hz-1s.mp3"));
    REQUIRE(tags.hasValue());
    CHECK(tags.value().codec == QStringLiteral("MP3"));
    CHECK(tags.value().durationMs > 900);
}

TEST_CASE("AC-08-5：損毀與被鎖住的檔案回傳原因，不丟例外") {
    const QString dir = freshTestDir(QStringLiteral("tags-bad"));
    writeCorrupt(dir + QStringLiteral("\\壞掉.flac"));
    const auto corrupt = readTags(dir + QStringLiteral("\\壞掉.flac"));
    REQUIRE_FALSE(corrupt.hasValue());
    CHECK_FALSE(corrupt.error().message.isEmpty());

    const QString locked = dir + QStringLiteral("\\鎖住.flac");
    writeFlac(locked, {});
    // 不共用任何存取：模擬別的程式獨占或沒有權限
    HANDLE handle = CreateFileW(locked.toStdWString().c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    const auto result = readTags(locked);
    CloseHandle(handle);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("無法開啟檔案")));

    const auto missing = readEmbeddedLyrics(dir + QStringLiteral("\\不存在.flac"));
    CHECK_FALSE(missing.hasValue());
}

TEST_CASE("AC-09-2：搜尋正規化不分大小寫與全形半形") {
    using auralis::normalizeSearchText;
    CHECK(normalizeSearchText(QStringLiteral("ＮｉｅＲ：Automata")) ==
          QStringLiteral("nier:automata"));
    CHECK(normalizeSearchText(QStringLiteral("ＡＢＣ１２３")) == QStringLiteral("abc123"));
    CHECK(normalizeSearchText(QStringLiteral("岡部啓一")) == QStringLiteral("岡部啓一"));
}

TEST_CASE("AC-08-2：路徑比對鍵去掉長路徑前綴、統一分隔符與大小寫") {
    using auralis::pathKey;
    CHECK(pathKey(QStringLiteral("\\\\?\\D:\\Music\\A.flac")) ==
          QStringLiteral("d:\\music\\a.flac"));
    CHECK(pathKey(QStringLiteral("D:/Music/A.FLAC")) == QStringLiteral("d:\\music\\a.flac"));
    CHECK(pathKey(QStringLiteral("\\\\?\\UNC\\nas\\share\\x.dsf")) ==
          QStringLiteral("\\\\nas\\share\\x.dsf"));
    // NFD 的「é」（e＋結合重音）和 NFC 的相同
    CHECK(pathKey(QStringLiteral("C:\\Cafe\u0301")) == pathKey(QStringLiteral("C:\\Caf\u00e9")));
}
