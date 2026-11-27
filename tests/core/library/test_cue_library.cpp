// CUE 曲目入庫（AC-07-1、AC-07-3）：每軌一首、不重複列整軌檔、編碼、增量、唯讀。
#include "cue_fixtures.h"
#include "scan_fixture.h"

#include "library/library_query_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>

using auralis::TrackFilter;
using auralis::TrackInfo;
using namespace auralis::core;
using namespace auralis::test;

namespace {

const QString kBilingualAlbum = QStringLiteral("雙語測試專輯：夜と朝");
const QString kJapaneseAlbum = QStringLiteral("夜明けの街 オリジナル・サウンドトラック");
const QString kChineseAlbum = QStringLiteral("城市裡的四季 精選輯");

struct Library : ScanFixture {
    std::unique_ptr<LibraryQueryImpl> query;

    explicit Library(const QString& name)
        : ScanFixture(name), query(std::make_unique<LibraryQueryImpl>(databasePath)) {}

    QList<TrackInfo> album(const QString& title) {
        TrackFilter filter;
        filter.album = title;
        auto found = query->tracks(filter);
        REQUIRE(found.hasValue());
        return found.value();
    }

    QList<TrackInfo> all() {
        auto found = query->tracks({});
        REQUIRE(found.hasValue());
        return found.value();
    }
};

/// 5 秒的整軌 FLAC（44.1/16），標籤是整軌檔自己的（.cue 沒寫的欄位才會用到）。
void writeWholeFile(const QString& path) {
    writeFlac(path,
              {{QStringLiteral("ALBUM"), QStringLiteral("整軌檔的專輯標籤")},
               {QStringLiteral("ARTIST"), QStringLiteral("整軌檔的藝人")},
               {QStringLiteral("REPLAYGAIN_ALBUM_GAIN"), QStringLiteral("-7.50 dB")},
               {QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringLiteral("-7.50 dB")}},
              44100, 16, 44100 * kSampleAudioSeconds);
}

/// 範例：UTF-8 BOM、Shift-JIS（FILE 寫 .wav，實際是 .flac）、Big5 三張整軌專輯、
/// 一首一般的單曲，以及一個指向不存在音訊檔的 .cue（旁邊的 .flac 照常以整檔收錄）。
void buildCueLibrary(const QString& root) {
    writeWholeFile(root + QStringLiteral("\\雙語\\album.flac"));
    writeBytes(root + QStringLiteral("\\雙語\\album.cue"),
               cueFixture(QStringLiteral("utf8-bom.cue")));
    writeWholeFile(root + QStringLiteral("\\日本\\album.flac"));
    writeBytes(root + QStringLiteral("\\日本\\album.cue"),
               encodeCodePage(cueFixture(QStringLiteral("ja-source.cue")), 932));
    writeWholeFile(root + QStringLiteral("\\台灣\\album.flac"));
    writeBytes(root + QStringLiteral("\\台灣\\album.cue"),
               encodeCodePage(cueFixture(QStringLiteral("zh-source.cue")), 950));
    writeFlac(root + QStringLiteral("\\單曲\\single.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("單曲")}});
    writeFlac(root + QStringLiteral("\\壞的 CUE\\y.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("旁邊的單曲")}});
    writeBytes(root + QStringLiteral("\\壞的 CUE\\x.cue"),
               QByteArrayLiteral("FILE \"nope.wav\" WAVE\r\n  TRACK 01 AUDIO\r\n"
                                 "    INDEX 01 00:00:00\r\n"));
}

QStringList titles(const QList<TrackInfo>& tracks) {
    QStringList result;
    for (const auto& track : tracks) {
        result.append(track.title);
    }
    return result;
}

QByteArray fileHash(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) +
           QByteArray::number(file.fileTime(QFileDevice::FileModificationTime).toMSecsSinceEpoch());
}

void touchLater(const QString& path, int seconds) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(QDateTime::currentDateTime().addSecs(seconds),
                             QFileDevice::FileModificationTime));
}

} // namespace

TEST_CASE("AC-07-1：掃描到 .cue 與整軌檔時列出每一軌（曲名、藝人、順序），不列整軌檔本身") {
    Library lib(QStringLiteral("t-11-cue-list"));
    buildCueLibrary(lib.root);
    const QString cuePath = lib.root + QStringLiteral("\\雙語\\album.cue");
    const QByteArray before =
        fileHash(cuePath) + fileHash(lib.root + QStringLiteral("\\雙語\\album.flac"));
    const ScanReport report = lib.scan();
    CHECK_FALSE(report.cancelled);

    const auto tracks = lib.album(kBilingualAlbum);
    REQUIRE(tracks.size() == 3);
    CHECK(titles(tracks) == QStringList{QStringLiteral("序曲：開始"),
                                        QStringLiteral("夜明けの街を歩く"),
                                        QStringLiteral("終章 Ｅｎｄｉｎｇ")});
    CHECK(tracks[0].artist == QStringLiteral("測試樂團"));
    CHECK(tracks[1].artist == QStringLiteral("歌手 A"));
    CHECK(tracks[2].artist == QStringLiteral("測試樂團")); // 軌沒寫時用整張的 PERFORMER
    const qint64 expectedMs[] = {2000, 1493, 1507};
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK(tracks[i].ref.path == cuePath);
        CHECK(tracks[i].ref.subsong == static_cast<quint32>(i + 1));
        CHECK(tracks[i].trackNo == i + 1);
        CHECK(tracks[i].albumArtist == QStringLiteral("測試樂團"));
        CHECK(tracks[i].genre == QStringLiteral("Soundtrack"));
        CHECK(tracks[i].year == 2019);
        CHECK(tracks[i].codec == QStringLiteral("FLAC"));
        CHECK(tracks[i].sampleRate == 44100);
        CHECK(tracks[i].startCdFrame == kSampleTrackStart[i]);
        CHECK(tracks[i].endCdFrame == (i < 2 ? kSampleTrackStart[i + 1] : -1));
        CHECK(tracks[i].durationMs == expectedMs[i]);
        CHECK(tracks[i].rgAlbumGain == -7.5);
        CHECK_FALSE(tracks[i].rgTrackGain.has_value()); // 整軌檔的曲目增益不屬於單一軌
    }

    // 整軌檔本身不列；其他：三張各 3 軌＋兩首單曲（壞掉的 .cue 旁邊的 y.flac 照常收錄）
    const auto all = lib.all();
    CHECK(all.size() == 11);
    for (const auto& track : all) {
        CHECK_FALSE(track.ref.path.endsWith(QStringLiteral("album.flac")));
    }
    const auto found = lib.query->findByIdentity(cuePath.toUpper(), 2);
    REQUIRE(found.hasValue());
    REQUIRE(found.value().has_value());
    CHECK(found.value()->title == QStringLiteral("夜明けの街を歩く"));

    // 讀不到的 .cue 進報告；音樂檔與 .cue 一律唯讀
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].path.endsWith(QStringLiteral("x.cue")));
    CHECK(report.failures[0].error.contains(QStringLiteral("nope.wav")));
    CHECK(fileHash(cuePath) + fileHash(lib.root + QStringLiteral("\\雙語\\album.flac")) == before);
}

TEST_CASE("AC-07-3：Shift-JIS 與 Big5 的 .cue 入庫後曲名、藝人、專輯沒有亂碼") {
    Library lib(QStringLiteral("t-11-cue-encoding"));
    buildCueLibrary(lib.root);
    lib.scan();

    const auto japanese = lib.album(kJapaneseAlbum);
    REQUIRE(japanese.size() == 3);
    CHECK(titles(japanese) == QStringList{QStringLiteral("夜明けの街を歩く"),
                                          QStringLiteral("雨上がりの空と約束"),
                                          QStringLiteral("遠い記憶／静かな港町")});
    CHECK(japanese[0].artist == QStringLiteral("東京室内管弦楽団"));
    CHECK(japanese[0].genre == QStringLiteral("Anime"));

    const auto chinese = lib.album(kChineseAlbum);
    REQUIRE(chinese.size() == 3);
    CHECK(titles(chinese) == QStringList{QStringLiteral("晴天的午後"),
                                         QStringLiteral("雨中漫步的回憶"),
                                         QStringLiteral("遙遠的約定與燈火")});
    CHECK(chinese[0].albumArtist == QStringLiteral("臺北愛樂合唱團"));
    CHECK(chinese[0].genre == QStringLiteral("流行"));
}

TEST_CASE("AC-07-1：CUE 的增量掃描：沒變不重讀、改 .cue 或整軌檔時重讀、刪掉 .cue 後改列整軌檔") {
    Library lib(QStringLiteral("t-11-cue-incremental"));
    buildCueLibrary(lib.root);
    lib.scan();
    const QString cuePath = lib.root + QStringLiteral("\\雙語\\album.cue");
    const qint64 firstId = lib.album(kBilingualAlbum)[1].id;

    const ScanReport unchanged = lib.scan();
    CHECK(unchanged.added == 0);
    CHECK(unchanged.updated == 0);
    CHECK(unchanged.removed == 0);
    CHECK(unchanged.scanned == 9); // 4 個 .cue＋5 個 .flac 都算檢查過（含被 .cue 涵蓋的整軌檔）

    // .cue 改了第 2 軌的曲名、拿掉第 3 軌
    QByteArray edited = cueFixture(QStringLiteral("utf8-bom.cue"));
    edited.replace("夜明けの街を歩く", "新的第二軌");
    edited.truncate(edited.indexOf("  TRACK 03"));
    writeBytes(cuePath, edited);
    touchLater(cuePath, 120);
    const ScanReport changed = lib.scan();
    CHECK(changed.updated == 1);
    const auto tracks = lib.album(kBilingualAlbum);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[1].title == QStringLiteral("新的第二軌"));
    CHECK(tracks[1].id == firstId);    // 原地更新
    CHECK(tracks[1].endCdFrame == -1); // 現在是最後一軌

    // 只換整軌檔（修改時間變新）也會重讀
    touchLater(lib.root + QStringLiteral("\\雙語\\album.flac"), 240);
    CHECK(lib.scan().updated == 1);

    // 刪掉 .cue：整軌檔改以整檔收錄，CUE 曲目移除
    REQUIRE(QFile::remove(cuePath));
    const ScanReport removed = lib.scan();
    CHECK(removed.removed == 1);
    CHECK(removed.added == 1);
    CHECK(lib.album(kBilingualAlbum).isEmpty());
    const auto whole =
        lib.query->findByIdentity(lib.root + QStringLiteral("\\雙語\\album.flac"), 0);
    REQUIRE(whole.hasValue());
    CHECK(whole.value().has_value());
}

TEST_CASE("AC-07-1：兩個 .cue 指向同一個整軌檔時只收錄一份") {
    Library lib(QStringLiteral("t-11-cue-duplicate"));
    writeWholeFile(lib.root + QStringLiteral("\\雙語\\album.flac"));
    writeBytes(lib.root + QStringLiteral("\\雙語\\album.cue"),
               cueFixture(QStringLiteral("utf8-bom.cue")));
    writeBytes(lib.root + QStringLiteral("\\雙語\\album (copy).cue"),
               cueFixture(QStringLiteral("utf8-bom.cue")));
    const ScanReport report = lib.scan();
    CHECK(report.failures.isEmpty());
    const auto tracks = lib.all();
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[1].ref.path == tracks[0].ref.path);
    CHECK(tracks[2].ref.path == tracks[0].ref.path);
    CHECK(lib.scan().added == 0); // 被略過的那個 .cue 下次也不會被當成新檔
}

TEST_CASE("AC-07-1：一軌一檔的 .cue（間隔接在上一個 FILE）每軌一首，各音檔不另列整檔") {
    Library lib(QStringLiteral("t-112-cue-per-track"));
    const QString dir = lib.root + QStringLiteral("\\一軌一檔");
    for (const char* name : {"01.flac", "02.flac", "03.flac"}) {
        writeFlac(dir + QLatin1Char('\\') + QString::fromLatin1(name), {}, 44100, 16, 44100 * 2);
    }
    // 音檔放在 .cue 旁邊的那張收錄；.cue 放在只有 .cue 的子資料夾的那張照實說找不到音訊檔
    const QByteArray cue = QByteArrayLiteral("TITLE \"一軌一檔\"\r\n"
                                             "FILE \"01.wav\" WAVE\r\n"
                                             "  TRACK 01 AUDIO\r\n    INDEX 01 00:00:00\r\n"
                                             "  TRACK 02 AUDIO\r\n    INDEX 00 00:01:00\r\n"
                                             "FILE \"02.wav\" WAVE\r\n    INDEX 01 00:00:00\r\n"
                                             "  TRACK 03 AUDIO\r\n    INDEX 00 00:01:50\r\n"
                                             "FILE \"03.wav\" WAVE\r\n    INDEX 01 00:00:00\r\n");
    writeBytes(dir + QStringLiteral("\\album.cue"), cue);
    writeBytes(lib.root + QStringLiteral("\\另一張\\CUE\\album.cue"), cue);
    writeFlac(lib.root + QStringLiteral("\\另一張\\01.flac"), {}, 44100, 16, 44100);

    const ScanReport report = lib.scan();
    const auto tracks = lib.album(QStringLiteral("一軌一檔"));
    REQUIRE(tracks.size() == 3);
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK(tracks[i].ref.path == dir + QStringLiteral("\\album.cue"));
        CHECK(tracks[i].ref.subsong == static_cast<quint32>(i + 1));
        CHECK(tracks[i].durationMs == 2000); // 每軌是自己的整個音檔
        const auto audio = lib.query->audioFilePath(tracks[i].ref);
        REQUIRE(audio.hasValue());
        CHECK(audio.value() == dir + QStringLiteral("\\0%1.flac").arg(i + 1));
    }
    // 三首＋另一張的整檔 01.flac；一軌一檔的音檔本身不重複列
    CHECK(lib.all().size() == 4);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].path.endsWith(QStringLiteral("\\CUE\\album.cue")));
    CHECK(report.failures[0].error.contains(QStringLiteral("找不到 CUE 指向的音訊檔")));
}

TEST_CASE("T-77：audioFilePath 一般曲目回傳 ref.path 本身，單 FILE 的 CUE 每軌回傳整軌檔") {
    Library lib(QStringLiteral("t-77-audio-path"));
    buildCueLibrary(lib.root);
    lib.scan();
    const QString single = lib.root + QStringLiteral("\\單曲\\single.flac");
    const auto whole = lib.query->audioFilePath({single, 0});
    REQUIRE(whole.hasValue());
    CHECK(whole.value() == single);

    // FILE 寫 .wav、實際是 .flac 的那張也照播放的規則找到 .flac
    const QStringList albums{QStringLiteral("雙語"), QStringLiteral("日本")};
    for (const auto& album : albums) {
        CAPTURE(album);
        const QString cuePath =
            lib.root + QLatin1Char('\\') + album + QStringLiteral("\\album.cue");
        for (quint32 track = 1; track <= 3; ++track) {
            CAPTURE(track);
            const auto path = lib.query->audioFilePath({cuePath, track});
            REQUIRE(path.hasValue());
            CHECK(path.value() ==
                  lib.root + QLatin1Char('\\') + album + QStringLiteral("\\album.flac"));
        }
    }
}

TEST_CASE("T-77：audioFilePath 多個 FILE 的 CUE，不同軌回傳各自的整軌檔") {
    Library lib(QStringLiteral("t-77-audio-path-multi"));
    const QString dir = lib.root + QStringLiteral("\\兩片");
    writeWholeFile(dir + QStringLiteral("\\disc1.flac"));
    writeWholeFile(dir + QStringLiteral("\\disc2.flac"));
    writeBytes(dir + QStringLiteral("\\album.cue"),
               QByteArrayLiteral("TITLE \"兩片\"\r\n"
                                 "FILE \"disc1.flac\" WAVE\r\n"
                                 "  TRACK 01 AUDIO\r\n    INDEX 01 00:00:00\r\n"
                                 "  TRACK 02 AUDIO\r\n    INDEX 01 00:02:00\r\n"
                                 "FILE \"disc2.flac\" WAVE\r\n"
                                 "  TRACK 03 AUDIO\r\n    INDEX 01 00:00:00\r\n"
                                 "  TRACK 04 AUDIO\r\n    INDEX 01 00:03:00\r\n"));
    const ScanReport report = lib.scan();
    REQUIRE(report.failures.isEmpty());
    REQUIRE(lib.all().size() == 4);
    const QString cuePath = dir + QStringLiteral("\\album.cue");
    const QString expected[] = {
        dir + QStringLiteral("\\disc1.flac"), dir + QStringLiteral("\\disc1.flac"),
        dir + QStringLiteral("\\disc2.flac"), dir + QStringLiteral("\\disc2.flac")};
    for (quint32 track = 1; track <= 4; ++track) {
        CAPTURE(track);
        const auto path = lib.query->audioFilePath({cuePath, track});
        REQUIRE(path.hasValue());
        CHECK(path.value() == expected[track - 1]);
    }
    // 大小寫、分隔符不同的身分也找得到（同 findByIdentity）
    const auto upper = lib.query->audioFilePath(
        {cuePath.toUpper().replace(QLatin1Char('\\'), QLatin1Char('/')), 3});
    REQUIRE(upper.hasValue());
    CHECK(upper.value().compare(dir + QStringLiteral("\\disc2.flac"), Qt::CaseInsensitive) == 0);
}

TEST_CASE("T-77：audioFilePath 在 .cue 指的檔案不存在、或曲目不在音樂庫時回傳中文錯誤") {
    Library lib(QStringLiteral("t-77-audio-path-errors"));
    buildCueLibrary(lib.root);
    lib.scan();
    const QString cuePath = lib.root + QStringLiteral("\\台灣\\album.cue");
    REQUIRE(lib.query->audioFilePath({cuePath, 1}).hasValue());

    // 入庫之後整軌檔不見了（測試時產生的檔案）
    REQUIRE(QFile::remove(lib.root + QStringLiteral("\\台灣\\album.flac")));
    const auto missing = lib.query->audioFilePath({cuePath, 1});
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message.contains(QStringLiteral("找不到 CUE 指向的音訊檔")));

    // 不在音樂庫：沒掃過的檔案、.cue 裡沒有的軌號、讀不到而沒入庫的 .cue
    const QString notInLibrary[] = {lib.root + QStringLiteral("\\沒有這首.flac"),
                                    lib.root + QStringLiteral("\\雙語\\album.cue"),
                                    lib.root + QStringLiteral("\\壞的 CUE\\x.cue")};
    const quint32 subsongs[] = {0, 9, 1};
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        const auto result = lib.query->audioFilePath({notInLibrary[i], subsongs[i]});
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().message.contains(QStringLiteral("音樂庫裡沒有這首曲目")));
    }
}
