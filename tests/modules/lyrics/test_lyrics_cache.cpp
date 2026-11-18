// lyrics_cache（AC-15-1）：找到與沒找到都記；曲目檔案（CUE 看整軌檔）修改時間改變、.lrc
// 有變化時失效。
#include "lyr_test_helpers.h"
#include "lyrics_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

void touch(const QString& path, const QDateTime& when) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(when, QFileDevice::FileModificationTime));
}

LyricsTrack trackAt(const QString& audio) {
    LyricsTrack t;
    t.ref = {audio, 0};
    t.audioPath = audio;
    return t;
}

LyricsDocument found(LyricsSource source, const QString& text) {
    LyricsDocument doc = parseLyrics(text);
    doc.source = source;
    return doc;
}

const QDateTime kOld = QDateTime::currentDateTime().addSecs(-3600);

} // namespace

TEST_CASE("AC-15-1：找到的歌詞寫進 lyrics_cache，讀回的行、來源、同步都相同") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"曲.flac"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    const LyricsCache cache(*db.db);

    const LyricsDocument doc = found(LyricsSource::Embedded, u"[offset:200]\n[00:01.00]一\n"_s);
    REQUIRE(cache.write(trackAt(audio), doc));
    const auto back = cache.read(trackAt(audio));
    REQUIRE(back.has_value());
    CHECK(back->status == LyricsDocument::Status::Found);
    CHECK(back->source == LyricsSource::Embedded);
    CHECK(back->synced);
    CHECK(back->lines == QList<LyricLine>{{800, u"一"_s}});

    // 資料庫裡的列：鍵用 pathKey，來源與狀態照規格的文字
    const auto rows =
        db.db->query(u"SELECT path_key, subsong, source, synced, status FROM lyrics_cache"_s);
    REQUIRE(rows.hasValue());
    REQUIRE(rows.value().size() == 1);
    CHECK(rows.value()[0][0].toString() == pathKey(audio));
    CHECK(rows.value()[0][2].toString() == u"embedded"_s);
    CHECK(rows.value()[0][3].toInt() == 1);
    CHECK(rows.value()[0][4].toString() == u"found"_s);
}

TEST_CASE("AC-15-1：沒找到也記（notfound），讀回來是 NotFound") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"none.flac"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    const LyricsCache cache(*db.db);
    LyricsDocument none;
    none.status = LyricsDocument::Status::NotFound;
    REQUIRE(cache.write(trackAt(audio), none));
    const auto back = cache.read(trackAt(audio));
    REQUIRE(back.has_value());
    CHECK(back->status == LyricsDocument::Status::NotFound);
    CHECK(back->lines.isEmpty());
    const auto rows = db.db->query(u"SELECT status, content FROM lyrics_cache"_s);
    REQUIRE(rows.hasValue());
    CHECK(rows.value()[0][0].toString() == u"notfound"_s);
    CHECK(rows.value()[0][1].isNull());
}

TEST_CASE("AC-15-1：曲目檔案的修改時間改變後快取失效") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"a.flac"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    const LyricsCache cache(*db.db);
    REQUIRE(cache.write(trackAt(audio), found(LyricsSource::Embedded, u"內嵌"_s)));
    REQUIRE(cache.read(trackAt(audio)).has_value());

    touch(audio, QDateTime::currentDateTime().addSecs(10)); // 例：改了標籤
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());
    REQUIRE(QFile::remove(audio)); // 檔案不見了也不用快取
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());
}

TEST_CASE("AC-15-1：CUE 曲目看整軌檔的修改時間，鍵是 .cue 與軌號") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString cue = dir->filePath(u"Album.cue"_s);
    const QString audio = dir->filePath(u"Album.flac"_s);
    writeFile(cue, "cue");
    writeFile(audio, "audio");
    touch(cue, kOld);
    touch(audio, kOld);
    LyricsTrack t2;
    t2.ref = {cue, 2};
    t2.cueTrack = 2;
    t2.audioPath = audio;
    LyricsTrack t3 = t2;
    t3.ref.subsong = 3;
    t3.cueTrack = 3;
    const LyricsCache cache(*db.db);
    // 來源是 lrc 的 CUE 曲目一律重讀 .lrc（見下一個測試），所以這裡用 notfound
    LyricsDocument none;
    REQUIRE(cache.write(t2, none));
    CHECK_FALSE(cache.read(t3).has_value()); // 不同軌不共用
    REQUIRE(cache.read(t2).has_value());
    touch(audio, QDateTime::currentDateTime().addSecs(10));
    CHECK_FALSE(cache.read(t2).has_value());
}

TEST_CASE("AC-15-1：後來放進同名 .lrc、.lrc 被改或刪掉時，快取失效") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"s.flac"_s);
    const QString lrc = dir->filePath(u"s.lrc"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    const LyricsCache cache(*db.db);

    LyricsDocument none;
    REQUIRE(cache.write(trackAt(audio), none));
    REQUIRE(cache.read(trackAt(audio)).has_value());
    writeFile(lrc, "[00:01.00]新放的");
    touch(lrc, kOld); // 複製來的檔案保留舊的修改時間，也要失效
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());

    REQUIRE(cache.write(trackAt(audio), found(LyricsSource::Lrc, u"[00:01.00]新放的"_s)));
    REQUIRE(cache.read(trackAt(audio)).has_value());
    touch(lrc, QDateTime::currentDateTime().addSecs(10));
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());

    REQUIRE(cache.write(trackAt(audio), found(LyricsSource::Lrc, u"[00:01.00]新放的"_s)));
    REQUIRE(QFile::remove(lrc));
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());
}

TEST_CASE("AC-15-1：CUE 曲目有兩個 .lrc 候選時，換了被選中的 .lrc 快取就失效") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"Album.flac"_s);
    const QString own = dir->filePath(u"Album 03.lrc"_s);
    const QString whole = dir->filePath(u"Album.lrc"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    LyricsTrack t3;
    t3.ref = {dir->filePath(u"Album.cue"_s), 3};
    t3.cueTrack = 3;
    t3.audioPath = audio;
    const LyricsCache cache(*db.db);

    SECTION("快取來自 Album 03.lrc，刪掉它後只剩比快取舊的 Album.lrc") {
        writeFile(own, "[00:01.00]專屬");
        writeFile(whole, "[00:01.00]整軌");
        touch(own, kOld);
        touch(whole, kOld);
        REQUIRE(cache.write(t3, found(LyricsSource::Lrc, u"[00:01.00]專屬"_s)));
        REQUIRE(QFile::remove(own));
        CHECK_FALSE(cache.read(t3).has_value()); // 不能繼續顯示已刪掉那份的歌詞
    }
    SECTION("快取來自整軌 Album.lrc，之後放進修改時間較舊的 Album 03.lrc") {
        writeFile(whole, "[00:01.00]整軌");
        touch(whole, kOld);
        REQUIRE(cache.write(t3, found(LyricsSource::Lrc, u"[00:01.00]整軌"_s)));
        writeFile(own, "[00:01.00]專屬");
        touch(own, kOld.addSecs(-60));           // 複製來的檔案保留舊的修改時間
        CHECK_FALSE(cache.read(t3).has_value()); // 要改讀專屬的 .lrc
    }
}

TEST_CASE("status 是 error 的列不當成快取") {
    LyrDatabase db;
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"e.flac"_s);
    writeFile(audio, "audio");
    touch(audio, kOld);
    LyricsDocument failed;
    failed.status = LyricsDocument::Status::Error;
    const LyricsCache cache(*db.db);
    REQUIRE(cache.write(trackAt(audio), failed));
    CHECK_FALSE(cache.read(trackAt(audio)).has_value());
}
