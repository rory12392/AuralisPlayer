// 內嵌同步歌詞：ID3v2 SYLT 框轉成 LRC 文字（AC-15-1）。測試檔都在測試時用 TagLib 產生。
#include "library/tag_reader.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>

#include <memory>

#include <taglib/dsffile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/wavfile.h>

using auralis::core::readEmbeddedLyrics;
using namespace auralis::test;
using Sylt = TagLib::ID3v2::SynchronizedLyricsFrame;

namespace {

TagLib::String tl(const QString& text) {
    return TagLib::String(text.toStdWString());
}

/// 複製沒有標籤的 MP3 樣本到測試資料夾（不改樣本本身）。
QString copyMp3(const QString& dirName) {
    const QString path = freshTestDir(dirName) + QStringLiteral("\\曲目.mp3");
    REQUIRE(QFile::copy(
        QString::fromUtf8(AURALIS_FIXTURES) + QStringLiteral("/audio/sine-440hz-1s.mp3"), path));
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

struct SyltSpec {
    TagLib::String::Type encoding = TagLib::String::UTF16;
    Sylt::TimestampFormat format = Sylt::AbsoluteMilliseconds;
    Sylt::Type type = Sylt::Lyrics;
    const char* language = "jpn";
    QList<QPair<unsigned int, QString>> lines;
};

std::unique_ptr<Sylt> makeSylt(const SyltSpec& spec) {
    auto frame = std::make_unique<Sylt>(spec.encoding);
    frame->setLanguage(TagLib::ByteVector(spec.language, 3));
    frame->setTimestampFormat(spec.format);
    frame->setType(spec.type);
    Sylt::SynchedTextList list;
    for (const auto& [ms, text] : spec.lines) {
        list.append(Sylt::SynchedText(ms, tl(text)));
    }
    frame->setSynchedText(list);
    return frame;
}

std::unique_ptr<TagLib::ID3v2::UnsynchronizedLyricsFrame> makeUslt(const QString& text) {
    auto frame = std::make_unique<TagLib::ID3v2::UnsynchronizedLyricsFrame>(TagLib::String::UTF8);
    frame->setLanguage("jpn");
    frame->setText(tl(text));
    return frame;
}

void addFrames(TagLib::ID3v2::Tag* tag, const QList<SyltSpec>& sylts, const QString& uslt) {
    REQUIRE(tag != nullptr);
    if (!uslt.isEmpty()) {
        tag->addFrame(makeUslt(uslt).release()); // 框交給標籤擁有
    }
    for (const auto& spec : sylts) {
        tag->addFrame(makeSylt(spec).release());
    }
}

void writeMp3Frames(const QString& path, const QList<SyltSpec>& sylts, const QString& uslt = {}) {
    TagLib::MPEG::File file(path.toStdWString().c_str());
    REQUIRE(file.isValid());
    addFrames(file.ID3v2Tag(true), sylts, uslt);
    REQUIRE(file.save());
}

const SyltSpec kJapanese{TagLib::String::UTF16,
                         Sylt::AbsoluteMilliseconds,
                         Sylt::Lyrics,
                         "jpn",
                         {{0, QStringLiteral("遺されたもの")},
                          {1500, QStringLiteral("ＮｉｅＲ 第二行")},
                          {61234, QStringLiteral("第三行")}}};
const QString kJapaneseLrc =
    QStringLiteral("[00:00.00]遺されたもの\n[00:01.50]ＮｉｅＲ 第二行\n[01:01.23]第三行");

} // namespace

TEST_CASE("AC-15-1：MP3 的 SYLT（毫秒、UTF-16）轉成 LRC，同時有 USLT 時優先 SYLT") {
    const QString path = copyMp3(QStringLiteral("sylt-basic"));
    writeMp3Frames(path, {kJapanese}, QStringLiteral("純文字歌詞"));
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == kJapaneseLrc);
}

TEST_CASE("AC-15-1：多個 SYLT 框取第一個內容型別是歌詞、毫秒單位的，照框的編碼解碼") {
    const QString path = copyMp3(QStringLiteral("sylt-multi"));
    SyltSpec events{TagLib::String::Latin1,
                    Sylt::AbsoluteMilliseconds,
                    Sylt::Events,
                    "eng",
                    {{0, QStringLiteral("intro")}}};
    SyltSpec mpegFrames{TagLib::String::Latin1,
                        Sylt::AbsoluteMpegFrames,
                        Sylt::Lyrics,
                        "eng",
                        {{38, QStringLiteral("frames")}}};
    SyltSpec latin1{TagLib::String::Latin1,
                    Sylt::AbsoluteMilliseconds,
                    Sylt::Lyrics,
                    "fra",
                    {{2000, QStringLiteral("Café crème")}, {4500, QStringLiteral("Deuxième")}}};
    SyltSpec utf8{TagLib::String::UTF8,
                  Sylt::AbsoluteMilliseconds,
                  Sylt::Lyrics,
                  "zho",
                  {{100, QStringLiteral("第二個歌詞框")}}};
    writeMp3Frames(path, {events, mpegFrames, latin1, utf8});
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("[00:02.00]Café crème\n[00:04.50]Deuxième"));
}

TEST_CASE("AC-15-1：UTF-8 的 SYLT 照框的編碼解碼") {
    const QString path = copyMp3(QStringLiteral("sylt-utf8"));
    writeMp3Frames(path, {SyltSpec{TagLib::String::UTF8,
                                   Sylt::AbsoluteMilliseconds,
                                   Sylt::Lyrics,
                                   "zho",
                                   {{3005, QStringLiteral("繁體中文 UTF-8")}}}});
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("[00:03.00]繁體中文 UTF-8"));
}

TEST_CASE("AC-15-1：空的 SYLT 框與 MPEG 影格單位的框略過，退回 USLT") {
    const QString path = copyMp3(QStringLiteral("sylt-empty"));
    SyltSpec empty{TagLib::String::UTF16, Sylt::AbsoluteMilliseconds, Sylt::Lyrics, "jpn", {}};
    SyltSpec mpegFrames{TagLib::String::Latin1,
                        Sylt::AbsoluteMpegFrames,
                        Sylt::Lyrics,
                        "eng",
                        {{38, QStringLiteral("frames")}}};
    writeMp3Frames(path, {empty, mpegFrames}, QStringLiteral("純文字第一行\n純文字第二行"));
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("純文字第一行\n純文字第二行"));
}

const SyltSpec kBlank{
    TagLib::String::UTF16,
    Sylt::AbsoluteMilliseconds,
    Sylt::Lyrics,
    "jpn",
    {{0, QString()}, {1000, QStringLiteral("  ")}, {2000, QStringLiteral("\r\n")}}};

TEST_CASE("AC-15-1：每段文字都是空白的 SYLT 框不遮住 USLT") {
    const QString path = copyMp3(QStringLiteral("sylt-blank-uslt"));
    writeMp3Frames(path, {kBlank}, QStringLiteral("純文字歌詞"));
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("純文字歌詞"));
}

TEST_CASE("AC-15-1：每段文字都是空白的 SYLT 框略過，改用後面有效的 SYLT 框") {
    const QString path = copyMp3(QStringLiteral("sylt-blank-next"));
    writeMp3Frames(path, {kBlank, kJapanese});
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == kJapaneseLrc);
}

TEST_CASE("AC-15-1：有效 SYLT 框裡的空白時間點保留（清空顯示用）") {
    const QString path = copyMp3(QStringLiteral("sylt-blank-inner"));
    writeMp3Frames(path,
                   {SyltSpec{TagLib::String::UTF16,
                             Sylt::AbsoluteMilliseconds,
                             Sylt::Lyrics,
                             "jpn",
                             {{0, QStringLiteral("第一行")},
                              {1000, QStringLiteral(" ")},
                              {2000, QStringLiteral("第二行")}}}},
                   QStringLiteral("純文字"));
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("[00:00.00]第一行\n[00:01.00]\n[00:02.00]第二行"));
}

TEST_CASE("AC-15-1：沒有 SYLT 的 MP3 行為不變（USLT 照讀、沒有歌詞時空字串）") {
    const QString onlyUslt = copyMp3(QStringLiteral("sylt-none"));
    writeMp3Frames(onlyUslt, {}, QStringLiteral("只有純文字"));
    const auto lyrics = readEmbeddedLyrics(onlyUslt);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("只有純文字"));

    const auto bare = readEmbeddedLyrics(QString::fromUtf8(AURALIS_FIXTURES) +
                                         QStringLiteral("/audio/sine-440hz-1s.mp3"));
    REQUIRE(bare.hasValue());
    CHECK(bare.value().isEmpty());
}

TEST_CASE("AC-15-1：SYLT 的文字有換行時一個時間標籤只佔一行") {
    const QString path = copyMp3(QStringLiteral("sylt-newline"));
    writeMp3Frames(path, {SyltSpec{TagLib::String::UTF16,
                                   Sylt::AbsoluteMilliseconds,
                                   Sylt::Lyrics,
                                   "jpn",
                                   {{0, QStringLiteral("\n第一行")},
                                    {1000, QStringLiteral("第二行\r\n續")}}}});
    const auto lyrics = readEmbeddedLyrics(path);
    REQUIRE(lyrics.hasValue());
    CHECK(lyrics.value() == QStringLiteral("[00:00.00]第一行\n[00:01.00]第二行 續"));
}

TEST_CASE("AC-15-1：WAV 的 ID3 chunk 與 DSF 的 ID3v2 也讀 SYLT") {
    const QString dir = freshTestDir(QStringLiteral("sylt-wav-dsf"));
    const QString wav = dir + QStringLiteral("\\a.wav");
    writeWav(wav, {});
    {
        TagLib::RIFF::WAV::File file(wav.toStdWString().c_str());
        REQUIRE(file.isValid());
        addFrames(file.ID3v2Tag(), {kJapanese}, QStringLiteral("純文字"));
        REQUIRE(file.save());
    }
    const auto wavLyrics = readEmbeddedLyrics(wav);
    REQUIRE(wavLyrics.hasValue());
    CHECK(wavLyrics.value() == kJapaneseLrc);

    const QString dsf = dir + QStringLiteral("\\a.dsf");
    writeDsf(dsf, {});
    {
        TagLib::DSF::File file(dsf.toStdWString().c_str());
        REQUIRE(file.isValid());
        addFrames(file.tag(), {kJapanese}, {});
        REQUIRE(file.save());
    }
    const auto dsfLyrics = readEmbeddedLyrics(dsf);
    REQUIRE(dsfLyrics.hasValue());
    CHECK(dsfLyrics.value() == kJapaneseLrc);
}

TEST_CASE("AC-15-1：畸形的 SYLT 框不讓讀取當掉") {
    // ID3v2.3 標頭＋一個宣告 UTF-16、內容被截斷（文字沒有結尾、沒有時間）的 SYLT 框，後面接 MP3
    QFile mp3(QString::fromUtf8(AURALIS_FIXTURES) + QStringLiteral("/audio/sine-440hz-1s.mp3"));
    REQUIRE(mp3.open(QIODevice::ReadOnly));
    const QByteArray body = QByteArray::fromHex("01656e670201fffe4100420043");
    QByteArray frame("SYLT");
    frame.append(char(0)).append(char(0)).append(char(0)).append(char(body.size()));
    frame.append(char(0)).append(char(0)).append(body);
    QByteArray bytes("ID3");
    bytes.append(char(3)).append(char(0)).append(char(0));
    bytes.append(char(0)).append(char(0)).append(char(0)).append(char(frame.size()));
    bytes.append(frame).append(mp3.readAll());
    const QString path = freshTestDir(QStringLiteral("sylt-bad")) + QStringLiteral("\\壞框.mp3");
    writeBytes(path, bytes);
    const auto lyrics = readEmbeddedLyrics(path);
    CHECK(lyrics.hasValue());
}
