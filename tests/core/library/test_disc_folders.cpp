// 多片專輯：Disc／Disk／CD＋數字的子資料夾改用上一層資料夾分組（T-91，M-02 驗證 F-3）。
#include "library/library_scanner.h"
#include "library/library_writer.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>

using namespace auralis::core;
using namespace auralis::test;

namespace {

struct Library {
    QString root;
    std::unique_ptr<SqliteConnection> db;
    QList<LibraryRoot> roots;

    explicit Library(const QString& name) {
        const QString dir = freshTestDir(name);
        root = dir + QStringLiteral("\\音樂");
        REQUIRE(migrateDatabase(dir + QStringLiteral("\\library.db")).hasValue());
        auto opened = SqliteConnection::open(dir + QStringLiteral("\\library.db"));
        REQUIRE(opened.hasValue());
        db = std::move(opened).value();
        LibraryWriter writer(*db);
        roots.append({writer.ensureRoot(root), root});
    }

    ScanReport scan() { return scanLibrary(*db, roots, {}); }

    QList<QVariantList> rows(const QString& sql) {
        const auto result = db->query(sql);
        REQUIRE(result.hasValue());
        return result.value();
    }

    qint64 count(const QString& sql) { return rows(sql).first().first().toLongLong(); }

    qint64 folderId(const QString& relative) {
        const auto found = db->query(QStringLiteral("SELECT id FROM folders WHERE path = ?"),
                                     {root + QLatin1Char('\\') + relative});
        REQUIRE(found.hasValue());
        REQUIRE(found.value().size() == 1);
        return found.value().first().first().toLongLong();
    }

    void exec(const QString& sql) { REQUIRE(db->executeScript(sql).hasValue()); }
};

Tags disc(const QString& album, int discNo, int trackNo) {
    return {{QStringLiteral("ALBUM"), album},
            {QStringLiteral("ALBUMARTIST"), QStringLiteral("雙碟藝人")},
            {QStringLiteral("ARTIST"), QStringLiteral("雙碟藝人")},
            {QStringLiteral("DISCNUMBER"), QString::number(discNo)},
            {QStringLiteral("TRACKNUMBER"), QString::number(trackNo)},
            {QStringLiteral("TITLE"), QStringLiteral("曲 %1-%2").arg(discNo).arg(trackNo)}};
}

/// <album 資料夾>\<disc1>\01.flac、02.flac 與 <album 資料夾>\<disc2>\01.flac、02.flac。
void writeTwoDiscs(const QString& albumDir, const QString& disc1, const QString& disc2,
                   const QString& album = QStringLiteral("雙碟測試專輯")) {
    for (int track = 1; track <= 2; ++track) {
        writeFlac(albumDir + QLatin1Char('\\') + disc1 + QStringLiteral("\\0%1.flac").arg(track),
                  disc(album, 1, track));
        writeFlac(albumDir + QLatin1Char('\\') + disc2 + QStringLiteral("\\0%1.flac").arg(track),
                  disc(album, 2, track));
    }
}

} // namespace

TEST_CASE("T-91 AC-1：Disc／Disk／CD＋數字的資料夾名稱（不分大小寫、全半形、分隔可有可無）") {
    for (const char16_t* name : {u"Disc 1", u"disc1", u"DISC 01", u"CD.2", u"cd2", u"Disk_03",
                                 u"disk-4", u"ＣＤ２", u"Ｄｉｓｃ　１", u"CD 10", u"Disc. 2"}) {
        INFO(QString::fromUtf16(name).toStdString());
        CHECK(isDiscFolderName(QString::fromUtf16(name)));
    }
    for (const char16_t* name :
         {u"Disc 1 - Bonus", u"CD1 [Live]", u"Disc", u"CD", u"Discography", u"CDs 1", u"1 Disc",
          u"Disc One", u"ディスク1", u"Disc 1.5", u"アルバム", u"Disc 2 (Live)"}) {
        INFO(QString::fromUtf16(name).toStdString());
        CHECK_FALSE(isDiscFolderName(QString::fromUtf16(name)));
    }
}

TEST_CASE("T-91 AC-2：Disc 1／Disc 2 子資料夾的曲目歸成一張專輯，碟號保留") {
    Library lib(QStringLiteral("t-91-merge"));
    writeTwoDiscs(lib.root + QStringLiteral("\\藝人\\雙碟測試專輯"), QStringLiteral("Disc 1"),
                  QStringLiteral("Disc 2"));
    // 名稱寫法不同的另一張（全形、底線）也合併
    writeTwoDiscs(lib.root + QStringLiteral("\\藝人\\另一張"), QStringLiteral("ＣＤ１"),
                  QStringLiteral("cd_2"), QStringLiteral("另一張"));
    lib.scan();

    const auto albums = lib.rows(QStringLiteral(
        "SELECT title, folder_id, track_count FROM albums ORDER BY title COLLATE BINARY"));
    REQUIRE(albums.size() == 2);
    CHECK(albums[0][0].toString() == QStringLiteral("另一張"));
    CHECK(albums[0][1].toLongLong() == lib.folderId(QStringLiteral("藝人\\另一張")));
    CHECK(albums[0][2].toInt() == 4);
    CHECK(albums[1][0].toString() == QStringLiteral("雙碟測試專輯"));
    CHECK(albums[1][1].toLongLong() == lib.folderId(QStringLiteral("藝人\\雙碟測試專輯")));
    CHECK(albums[1][2].toInt() == 4);
    CHECK(lib.count(QStringLiteral("SELECT count(DISTINCT disc_no) FROM tracks t JOIN albums a "
                                   "ON a.id = t.album_id WHERE a.title = '雙碟測試專輯'")) == 2);
}

TEST_CASE("T-91 追加：同一層的子資料夾專輯相同、碟號互不重疊時合成一張（資料夾名稱不限）") {
    Library lib(QStringLiteral("t-91-discno"));
    writeTwoDiscs(lib.root + QStringLiteral("\\附錄"), QStringLiteral("Disc 1 - Bonus"),
                  QStringLiteral("Disc 2 - Bonus"), QStringLiteral("附錄"));
    writeTwoDiscs(lib.root + QStringLiteral("\\日文"), QStringLiteral("ディスク1"),
                  QStringLiteral("ディスク2"), QStringLiteral("日文"));
    lib.scan();

    const auto albums =
        lib.rows(QStringLiteral("SELECT title, folder_id, track_count FROM albums ORDER BY title"));
    REQUIRE(albums.size() == 2);
    CHECK(albums[0][1].toLongLong() == lib.folderId(QStringLiteral("日文")));
    CHECK(albums[0][2].toInt() == 4);
    CHECK(albums[1][1].toLongLong() == lib.folderId(QStringLiteral("附錄")));
    CHECK(albums[1][2].toInt() == 4);
}

TEST_CASE("T-91 AC-2：同名專輯的碟號重疊、或有曲目沒有碟號時照舊分開（不同版本）") {
    Library lib(QStringLiteral("t-91-separate"));
    // 規格 library-browse §1 情況 2：同名不同資料夾＝不同版本；兩邊都有碟 1
    writeTwoDiscs(lib.root + QStringLiteral("\\初回盤"), QStringLiteral("一"), QStringLiteral("二"),
                  QStringLiteral("同名"));
    writeFlac(lib.root + QStringLiteral("\\通常盤\\01.flac"), disc(QStringLiteral("同名"), 1, 1));
    // 沒有碟號標籤時無從判斷，不合併
    Tags noDisc = disc(QStringLiteral("無碟號"), 1, 1);
    noDisc.remove(QStringLiteral("DISCNUMBER"));
    writeFlac(lib.root + QStringLiteral("\\甲\\01.flac"), noDisc);
    writeFlac(lib.root + QStringLiteral("\\乙\\01.flac"), disc(QStringLiteral("無碟號"), 2, 1));
    lib.scan();

    // 初回盤：「一」「二」碟號不重疊，合成一張；它和通常盤的碟 1 重疊，仍分開
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM albums WHERE title = '同名'")) == 2);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM albums WHERE title = '同名' "
                                   "AND track_count = 4")) == 1);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM albums WHERE title = '無碟號'")) == 2);
}

TEST_CASE("T-91 追加：依碟號合併後再掃描，改過的檔案仍歸回同一張、專輯 id 不變") {
    Library lib(QStringLiteral("t-91-discno-rescan"));
    const QString album = lib.root + QStringLiteral("\\附錄");
    writeTwoDiscs(album, QStringLiteral("Disc 1 - Bonus"), QStringLiteral("Disc 2 - Bonus"),
                  QStringLiteral("附錄"));
    lib.scan();
    const auto before = lib.rows(QStringLiteral("SELECT id FROM albums"));
    REQUIRE(before.size() == 1);

    // 重寫一個檔案（大小或修改時間改變）：增量掃描重讀它
    writeFlac(album + QStringLiteral("\\Disc 2 - Bonus\\02.flac"),
              disc(QStringLiteral("附錄"), 2, 2), 48000);
    const ScanReport report = lib.scan();
    CHECK(report.updated == 1);
    CHECK(lib.rows(QStringLiteral("SELECT id FROM albums")) == before);
    CHECK(lib.count(QStringLiteral("SELECT track_count FROM albums")) == 4);
}

TEST_CASE("T-91 AC-3：舊規則拆開的專輯在下一次增量掃描自動合併，曲目 id 與 rg_computed 不變") {
    Library lib(QStringLiteral("t-91-fixup"));
    writeTwoDiscs(lib.root + QStringLiteral("\\雙碟測試專輯"), QStringLiteral("Disc 1"),
                  QStringLiteral("Disc 2"));
    lib.scan();
    REQUIRE(lib.count(QStringLiteral("SELECT count(*) FROM albums")) == 1);

    // 照舊規則（專輯資料夾＝檔案所在的資料夾）重建 albums，做出升級前的資料
    lib.exec(QStringLiteral(
        "UPDATE tracks SET album_id = NULL;"
        "DELETE FROM albums;"
        "INSERT INTO albums(album_artist, title, folder_id) SELECT DISTINCT t.album_artist, "
        "t.album, f.folder_id FROM tracks t JOIN files f ON f.id = t.file_id;"
        "UPDATE tracks SET album_id = (SELECT a.id FROM albums a JOIN files f ON f.folder_id = "
        "a.folder_id WHERE f.id = tracks.file_id AND a.title = tracks.album);"
        "UPDATE albums SET track_count = (SELECT count(*) FROM tracks WHERE album_id = albums.id);"
        "INSERT INTO rg_computed(path_key, subsong, track_gain, computed_at) "
        "SELECT path_key, 0, -6.5, 1 FROM files;"));
    REQUIRE(lib.count(QStringLiteral("SELECT count(*) FROM albums")) == 2);
    const auto trackIdsBefore = lib.rows(QStringLiteral("SELECT id FROM tracks ORDER BY id"));

    const ScanReport report = lib.scan(); // 檔案都沒變：增量掃描不重讀任何檔案
    CHECK(report.updated == 0);
    CHECK(report.added == 0);

    const auto albums =
        lib.rows(QStringLiteral("SELECT id, folder_id, track_count, cover_key FROM albums"));
    REQUIRE(albums.size() == 1);
    CHECK(albums[0][1].toLongLong() == lib.folderId(QStringLiteral("雙碟測試專輯")));
    CHECK(albums[0][2].toInt() == 4);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM tracks WHERE album_id IS NOT ") +
                    albums[0][0].toString()) == 0);
    CHECK(lib.rows(QStringLiteral("SELECT id FROM tracks ORDER BY id")) == trackIdsBefore);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM rg_computed WHERE track_gain = -6.5")) ==
          4);

    // 再掃一次不再變動
    lib.scan();
    CHECK(lib.rows(QStringLiteral("SELECT id FROM albums")) == QList<QVariantList>{{albums[0][0]}});
}
