// 掃描跳過符號連結與 junction：連結可以指到網路位置，跟著讀會對外連線，而本程式的對外連線只有
// 歌詞查詢。雲端硬碟的佔位檔也是重新解析點，但不是連結，照常收錄。
// 連結只建在測試暫存資料夾裡、只指到同一個暫存資料夾裡的檔案，不指到網路。
// 建立符號連結需要開發人員模式或系統管理員權限：建不起來時那一項 SKIP；junction 不需要權限。
#include "cue_fixtures.h"
#include "library_fixtures.h"

#include "library/cue_tracks.h"
#include "library/file_enumerator.h"
#include "library/library_scanner.h"
#include "library/migrator.h"
#include "library/open_paths.h"
#include "library/sqlite_connection.h"
#include "library/tag_reader.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>

#include <algorithm>
#include <memory>

#include <windows.h>

using namespace auralis::core;
using namespace auralis::test;

namespace {

const QString kLinkReason = QStringLiteral("符號連結，已跳過");

struct Fixture {
    LinkDir links;
    QString dir;
    QString root;
    QString outside; ///< 音樂資料夾外面（同一個測試暫存資料夾裡），連結都指到這裡
    std::unique_ptr<SqliteConnection> db;
    QList<LibraryRoot> roots;

    explicit Fixture(const QString& name) : links(name) {
        dir = links.path;
        root = dir + QStringLiteral("\\音樂");
        outside = dir + QStringLiteral("\\外面");
        writeFlac(outside + QStringLiteral("\\target.flac"),
                  {{QStringLiteral("TITLE"), QStringLiteral("連結的目標")}});
        writeFlac(root + QStringLiteral("\\專輯\\a.flac"), {});
        const QString dbPath = dir + QStringLiteral("\\library.db");
        REQUIRE(migrateDatabase(dbPath).hasValue());
        auto opened = SqliteConnection::open(dbPath);
        REQUIRE(opened.hasValue());
        db = std::move(opened).value();
        LibraryWriter writer(*db);
        roots.append({writer.ensureRoot(root), root});
    }

    qint64 trackCount() {
        const auto rows = db->query(QStringLiteral("SELECT count(*) FROM tracks"));
        REQUIRE(rows.hasValue());
        return rows.value().first().first().toLongLong();
    }
};

/// 記下讀標籤時開過的檔案。
struct ReadSpy {
    QStringList read;
    ReadSpy() {
        setReadTagsFaultForTest([this](const QString& path) { read.append(path); });
    }
    ~ReadSpy() { setReadTagsFaultForTest({}); }
    ReadSpy(const ReadSpy&) = delete;
    ReadSpy& operator=(const ReadSpy&) = delete;
};

/// 記下 loadCue 查詢過的路徑。
struct ProbeSpy {
    QStringList probed;
    ProbeSpy() {
        setCueProbeHookForTest([this](const QString& path) {
            probed.append(QDir::toNativeSeparators(path));
            return true;
        });
    }
    ~ProbeSpy() { setCueProbeHookForTest({}); }
    ProbeSpy(const ProbeSpy&) = delete;
    ProbeSpy& operator=(const ProbeSpy&) = delete;
};

bool anyContains(const QStringList& paths, const QString& part) {
    return std::any_of(paths.cbegin(), paths.cend(), [&part](const QString& path) {
        return path.contains(part, Qt::CaseInsensitive);
    });
}

const ScanFailure* failureFor(const QList<ScanFailure>& failures, const QString& path) {
    const auto it = std::find_if(failures.cbegin(), failures.cend(),
                                 [&path](const ScanFailure& f) { return f.path == path; });
    return it == failures.cend() ? nullptr : &*it;
}

QByteArray cueText(const QString& file) {
    return (QStringLiteral("FILE \"%1\" WAVE\r\n  TRACK 01 AUDIO\r\n    TITLE \"一\"\r\n"
                           "    INDEX 01 00:00:00\r\n")
                .arg(file))
        .toUtf8();
}

} // namespace

TEST_CASE("AC-08-5：只有符號連結與 junction 算連結，雲端佔位檔等其他重新解析點不算") {
    const unsigned long reparse = FILE_ATTRIBUTE_REPARSE_POINT;
    CHECK(isLinkEntry(reparse, IO_REPARSE_TAG_SYMLINK));
    CHECK(isLinkEntry(reparse | FILE_ATTRIBUTE_DIRECTORY, IO_REPARSE_TAG_SYMLINK));
    CHECK(isLinkEntry(reparse | FILE_ATTRIBUTE_DIRECTORY, IO_REPARSE_TAG_MOUNT_POINT));
    // 雲端硬碟（OneDrive 等）的佔位檔：是重新解析點，但內容就是這個檔案自己
    for (const unsigned long tag :
         {IO_REPARSE_TAG_CLOUD, IO_REPARSE_TAG_CLOUD_1, IO_REPARSE_TAG_CLOUD_6,
          IO_REPARSE_TAG_CLOUD_F, IO_REPARSE_TAG_ONEDRIVE, IO_REPARSE_TAG_DEDUP, IO_REPARSE_TAG_WOF,
          IO_REPARSE_TAG_APPEXECLINK}) {
        INFO(tag);
        CHECK_FALSE(isLinkEntry(reparse | FILE_ATTRIBUTE_OFFLINE, tag));
    }
    // 沒有重新解析點屬性時，保留欄位的值沒有意義
    CHECK_FALSE(isLinkEntry(FILE_ATTRIBUTE_ARCHIVE, IO_REPARSE_TAG_SYMLINK));
}

TEST_CASE("AC-08-5：帶著非連結重新解析點的檔案（像雲端佔位檔）照常列出，不當成連結") {
    const QString root = freshTestDir(QStringLiteral("links-placeholder"));
    const QString marked = root + QStringLiteral("\\placeholder.flac");
    writeFlac(marked, {});
    writeFlac(root + QStringLiteral("\\plain.flac"), {});
    if (!setCustomReparsePoint(marked)) {
        SKIP("這個檔案系統不能設定自訂的重新解析點");
    }
    REQUIRE((GetFileAttributesW(marked.toStdWString().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) !=
            0);
    CHECK_FALSE(isLinkPath(marked));

    const Enumeration listing = enumerateAudioFiles(root, [] { return false; });
    QStringList listed;
    for (const auto& file : listing.files) {
        listed.append(file.path);
    }
    CHECK(listed.contains(marked));
    CHECK(listed.contains(root + QStringLiteral("\\plain.flac")));
    CHECK(listing.skippedLinks.isEmpty());
}

TEST_CASE("AC-08-5：資料夾裡的 junction 跳過並記進掃描報告，掃描不中斷") {
    Fixture fx(QStringLiteral("links-junction"));
    const QString junction = fx.root + QStringLiteral("\\連到外面");
    makeJunction(junction, fx.outside);
    REQUIRE(isLinkPath(junction));
    CHECK_FALSE(isLinkPath(fx.root + QStringLiteral("\\專輯")));
    CHECK_FALSE(isLinkPath(fx.root + QStringLiteral("\\不存在")));

    ReadSpy spy;
    const ScanReport report = scanLibrary(*fx.db, fx.roots, {});
    CHECK_FALSE(report.cancelled);
    CHECK(report.added == 1);
    CHECK(fx.trackCount() == 1);
    const ScanFailure* skipped = failureFor(report.failures, junction);
    REQUIRE(skipped != nullptr);
    CHECK(skipped->error == kLinkReason);
    CHECK(report.failed == 1);
    CHECK_FALSE(anyContains(spy.read, QStringLiteral("target.flac")));
}

TEST_CASE("AC-08-5：檔案與資料夾的符號連結跳過並記進掃描報告，目標不被讀取") {
    Fixture fx(QStringLiteral("links-symlink"));
    const QString fileLink = fx.root + QStringLiteral("\\專輯\\link.flac");
    const QString dirLink = fx.root + QStringLiteral("\\連結資料夾");
    if (!makeSymlink(fileLink, fx.outside + QStringLiteral("\\target.flac"), false) ||
        !makeSymlink(dirLink, fx.outside, true)) {
        SKIP("沒有建立符號連結的權限（要開發人員模式或系統管理員）");
    }
    REQUIRE(isLinkPath(fileLink));
    // 不是音樂檔也不是封面的連結不用列進報告
    REQUIRE(makeSymlink(fx.root + QStringLiteral("\\專輯\\notes.txt"),
                        fx.outside + QStringLiteral("\\target.flac"), false));

    ReadSpy spy;
    const ScanReport report = scanLibrary(*fx.db, fx.roots, {});
    CHECK_FALSE(report.cancelled);
    CHECK(report.added == 1);
    CHECK(fx.trackCount() == 1);
    CHECK(report.failed == 2);
    for (const QString& link : {fileLink, dirLink}) {
        INFO(link.toStdString());
        const ScanFailure* skipped = failureFor(report.failures, link);
        REQUIRE(skipped != nullptr);
        CHECK(skipped->error == kLinkReason);
    }
    CHECK_FALSE(anyContains(spy.read, QStringLiteral("link.flac")));
    CHECK_FALSE(anyContains(spy.read, QStringLiteral("target.flac")));
}

TEST_CASE("AC-08-5：以前收錄過的檔案換成符號連結後，從音樂庫移除") {
    Fixture fx(QStringLiteral("links-replaced"));
    const QString path = fx.root + QStringLiteral("\\專輯\\b.flac");
    writeFlac(path, {});
    CHECK(scanLibrary(*fx.db, fx.roots, {}).added == 2);
    REQUIRE(QFile::remove(path));
    if (!makeSymlink(path, fx.outside + QStringLiteral("\\target.flac"), false)) {
        SKIP("沒有建立符號連結的權限（要開發人員模式或系統管理員）");
    }
    const ScanReport report = scanLibrary(*fx.db, fx.roots, {});
    CHECK(report.removed == 1);
    CHECK(fx.trackCount() == 1);
    REQUIRE(failureFor(report.failures, path) != nullptr);
}

TEST_CASE("AC-08-5：資料夾封面圖檔是符號連結時不拿來當封面") {
    const LinkDir links(QStringLiteral("links-cover"));
    const QString root = links.path;
    writeFlac(root + QStringLiteral("\\專輯\\a.flac"), {});
    writeBytes(root + QStringLiteral("\\外面\\real.jpg"), "jpg");
    const QString cover = root + QStringLiteral("\\專輯\\cover.jpg");
    if (!makeSymlink(cover, root + QStringLiteral("\\外面\\real.jpg"), false)) {
        SKIP("沒有建立符號連結的權限（要開發人員模式或系統管理員）");
    }
    const Enumeration listing =
        enumerateAudioFiles(root + QStringLiteral("\\專輯"), [] { return false; });
    REQUIRE(listing.folders.size() == 1);
    CHECK(listing.folders.front().coverImage.isEmpty());
    REQUIRE(listing.skippedLinks.size() == 1);
    CHECK(listing.skippedLinks.front().path == cover);
}

TEST_CASE("AC-07-1：CUE 的 FILE 經過 junction 時拒絕，連結另一邊的檔案一次都不查詢") {
    const LinkDir links(QStringLiteral("links-cue"));
    const QString dir = links.path;
    const QString album = dir + QStringLiteral("\\音樂\\專輯");
    writeFlac(dir + QStringLiteral("\\外面\\whole.flac"), {});
    writeFlac(album + QStringLiteral("\\sub\\inner.flac"), {});
    const QString junction = album + QStringLiteral("\\sub\\連到外面");
    makeJunction(junction, dir + QStringLiteral("\\外面"));
    const QString cuePath = album + QStringLiteral("\\evil.cue");

    for (const auto& name :
         {QStringLiteral("sub\\連到外面\\whole.flac"), QStringLiteral("sub/連到外面/whole.wav"),
          QStringLiteral("sub\\連到外面\\深一層\\..\\whole.flac")}) {
        INFO(name.toStdString());
        writeBytes(cuePath, cueText(name));
        ProbeSpy probes;
        const auto cue = loadCue(cuePath);
        REQUIRE_FALSE(cue.hasValue());
        CHECK(cue.error().message.contains(QStringLiteral("符號連結")));
        CHECK(cue.error().message.contains(QStringLiteral("不開啟")));
        // 只查到連結本身為止，不查連結底下的任何路徑
        CHECK_FALSE(anyContains(probes.probed, QStringLiteral("連到外面\\")));
        CHECK_FALSE(anyContains(probes.probed, QStringLiteral("whole")));
    }

    SECTION("沒有經過連結的 FILE 照常找到") {
        writeBytes(cuePath, cueText(QStringLiteral("sub\\inner.flac")));
        CHECK(loadCue(cuePath).hasValue());
    }
}

TEST_CASE("AC-07-1：CUE 的 FILE 本身（或同主檔名的替代檔）是符號連結時不開啟") {
    const LinkDir links(QStringLiteral("links-cue-file"));
    const QString dir = links.path;
    const QString album = dir + QStringLiteral("\\音樂\\專輯");
    writeFlac(dir + QStringLiteral("\\外面\\whole.flac"), {});
    const QString link = album + QStringLiteral("\\whole.flac");
    if (!makeSymlink(link, dir + QStringLiteral("\\外面\\whole.flac"), false)) {
        SKIP("沒有建立符號連結的權限（要開發人員模式或系統管理員）");
    }
    const QString cuePath = album + QStringLiteral("\\evil.cue");
    // .wav 不存在時會找同主檔名的 .flac，那個也不能是連結
    for (const auto& name : {QStringLiteral("whole.flac"), QStringLiteral("whole.wav")}) {
        INFO(name.toStdString());
        writeBytes(cuePath, cueText(name));
        const auto cue = loadCue(cuePath);
        REQUIRE_FALSE(cue.hasValue());
    }
    writeBytes(cuePath, cueText(QStringLiteral("whole.flac")));
    CHECK(loadCue(cuePath).error().message.contains(QStringLiteral("符號連結")));
}

TEST_CASE("AC-11-5：拖進來的資料夾裡的連結跳過並列出原因，其他照常加入") {
    const LinkDir links(QStringLiteral("links-open"));
    const QString root = links.path;
    writeFlac(root + QStringLiteral("\\拖進來\\a.flac"), {});
    writeFlac(root + QStringLiteral("\\外面\\target.flac"), {});
    const QString junction = root + QStringLiteral("\\拖進來\\連到外面");
    makeJunction(junction, root + QStringLiteral("\\外面"));

    const OpenPathsResult result = expandOpenPaths({root + QStringLiteral("\\拖進來")});
    REQUIRE(result.tracks.size() == 1);
    CHECK(result.tracks.front().path == root + QStringLiteral("\\拖進來\\a.flac"));
    REQUIRE(result.skipped.size() == 1);
    CHECK(result.skipped.front().path == junction);
    CHECK(result.skipped.front().error == kLinkReason);
}
