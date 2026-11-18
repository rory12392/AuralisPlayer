// 音樂庫的外部輸入強化：.cue 的 FILE 只能指到 .cue 所在資料夾內（不連網路、不讀資料夾外），
// 單一壞檔（超大區塊、讀標籤時丟例外、超長路徑）不讓掃描中斷（AC-08-5）。
// 網路路徑只驗證「不會被查詢或開啟」：查檔的掛勾對資料夾外的路徑一律回答不存在，不會真的連線。
#include "library/cue_sheet.h"
#include "library/cue_tracks.h"
#include "library/library_scanner.h"
#include "library/migrator.h"
#include "library/read_only_stream.h"
#include "library/sqlite_connection.h"
#include "library/tag_reader.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>

#include <new>
#include <stdexcept>

using namespace auralis::core;
using namespace auralis::test;

namespace {

struct Fixture {
    QString dir;
    QString root;
    std::unique_ptr<SqliteConnection> db;
    QList<LibraryRoot> roots;

    explicit Fixture(const QString& name) {
        dir = freshTestDir(name);
        root = dir + QStringLiteral("\\音樂");
        const QString dbPath = dir + QStringLiteral("\\library.db");
        REQUIRE(migrateDatabase(dbPath).hasValue());
        auto opened = SqliteConnection::open(dbPath);
        REQUIRE(opened.hasValue());
        db = std::move(opened).value();
        LibraryWriter writer(*db);
        roots.append({writer.ensureRoot(root), root});
    }

    ScanReport scan() { return scanLibrary(*db, roots, {}); }

    qint64 count(const QString& sql) {
        const auto rows = db->query(sql);
        REQUIRE(rows.hasValue());
        return rows.value().first().first().toLongLong();
    }
};

bool under(const QString& path, const QString& folder) {
    return QDir::toNativeSeparators(path).startsWith(folder + QLatin1Char('\\'),
                                                     Qt::CaseInsensitive);
}

/// 記下 loadCue 查詢過的路徑；.cue 所在資料夾以外的一律回答「不存在」，所以就算檢查失效也不會連線。
struct ProbeRecorder {
    QStringList probed;
    explicit ProbeRecorder(const QString& allowedFolder) {
        setCueProbeHookForTest([this, allowedFolder](const QString& path) {
            probed.append(path);
            return under(path, allowedFolder);
        });
    }
    ~ProbeRecorder() { setCueProbeHookForTest({}); }
    ProbeRecorder(const ProbeRecorder&) = delete;
    ProbeRecorder& operator=(const ProbeRecorder&) = delete;
};

struct FaultGuard {
    explicit FaultGuard(std::function<void(const QString&)> fault) {
        setReadTagsFaultForTest(std::move(fault));
    }
    ~FaultGuard() { setReadTagsFaultForTest({}); }
    FaultGuard(const FaultGuard&) = delete;
    FaultGuard& operator=(const FaultGuard&) = delete;
};

QByteArray cueText(const QString& file) {
    return (QStringLiteral("FILE \"%1\" WAVE\r\n  TRACK 01 AUDIO\r\n    TITLE \"一\"\r\n"
                           "    INDEX 01 00:00:00\r\n")
                .arg(file))
        .toUtf8();
}

/// ID3v2 標頭宣告約 256 MB 的標籤，檔案本身 sizeBytes（其餘補零）。
void writeOversizedId3(const QString& path, qint64 sizeBytes) {
    writeBytes(path, QByteArrayLiteral("ID3\x04\x00\x00\x7f\x7f\x7f\x7f"));
    QFile file(path);
    REQUIRE(file.resize(sizeBytes));
}

} // namespace

TEST_CASE("AC-07-1：CUE 的 FILE 是網路、裝置路徑或在 .cue 所在資料夾以外時拒絕，且一次都不查詢") {
    const QString dir = freshTestDir(QStringLiteral("cue-confine"));
    const QString album = dir + QStringLiteral("\\音樂\\專輯");
    const QString outside = dir + QStringLiteral("\\外面\\whole.flac");
    writeFlac(outside, {});
    writeFlac(album + QStringLiteral("\\sub\\inner.flac"), {});

    const QStringList rejected{
        QStringLiteral("\\\\attacker.invalid\\share\\a.flac"),
        QStringLiteral("//attacker.invalid/share/a.flac"),
        QStringLiteral("\\/attacker.invalid\\share\\a.flac"),
        QStringLiteral("\\\\?\\UNC\\attacker.invalid\\share\\a.flac"),
        QStringLiteral("\\\\?\\") + outside,
        QStringLiteral("\\\\.\\pipe\\auralis-test"),
        QStringLiteral("..\\..\\外面\\whole.flac"),
        QStringLiteral("sub\\..\\..\\..\\外面\\whole.flac"),
        QStringLiteral("../../外面/whole.flac"),
        outside,
        QStringLiteral("CON"),
        QStringLiteral("sub\\nul.flac"),
        QStringLiteral("C:whole.flac"),
    };
    for (const auto& name : rejected) {
        INFO(name.toStdString());
        writeBytes(album + QStringLiteral("\\evil.cue"), cueText(name));
        ProbeRecorder probes(album);
        const auto cue = loadCue(album + QStringLiteral("\\evil.cue"));
        REQUIRE_FALSE(cue.hasValue());
        CHECK(cue.error().message.contains(QStringLiteral("不開啟")));
        CHECK(probes.probed.isEmpty());
    }
}

TEST_CASE("AC-07-1：CUE 的 FILE 是上標數字的 COM¹、LPT³ 或 CONIN$、CONOUT$ "
          "這類裝置名稱時拒絕，且一次都不查詢") {
    const QString dir = freshTestDir(QStringLiteral("cue-device-names"));
    const QString album = dir + QStringLiteral("\\音樂\\專輯");
    for (const auto& name :
         {QStringLiteral("COM¹"), QStringLiteral("sub\\com².flac"), QStringLiteral("LPT³.wav"),
          QStringLiteral("CONIN$"), QStringLiteral("sub\\conout$.flac")}) {
        INFO(name.toStdString());
        writeBytes(album + QStringLiteral("\\evil.cue"), cueText(name));
        ProbeRecorder probes(album);
        const auto cue = loadCue(album + QStringLiteral("\\evil.cue"));
        REQUIRE_FALSE(cue.hasValue());
        CHECK(cue.error().message.contains(QStringLiteral("裝置名稱")));
        CHECK(probes.probed.isEmpty());
    }
}

TEST_CASE("AC-07-1：CUE 的 FILE 指到同一資料夾或子資料夾（相對或絕對路徑）照常收錄") {
    const QString dir = freshTestDir(QStringLiteral("cue-confine-ok"));
    const QString album = dir + QStringLiteral("\\音樂\\專輯");
    const QString inner = album + QStringLiteral("\\sub\\inner.flac");
    writeFlac(inner, {});
    for (const auto& name :
         {QStringLiteral("sub\\inner.flac"), QStringLiteral("sub/inner.flac"),
          QStringLiteral(".\\sub\\x\\..\\inner.flac"), inner, QStringLiteral("sub\\inner.wav")}) {
        INFO(name.toStdString());
        writeBytes(album + QStringLiteral("\\ok.cue"), cueText(name));
        ProbeRecorder probes(album);
        const auto cue = loadCue(album + QStringLiteral("\\ok.cue"));
        REQUIRE(cue.hasValue());
        CHECK(cue.value().audioPaths.front().compare(inner, Qt::CaseInsensitive) == 0);
        for (const auto& path : probes.probed) {
            CHECK(under(path, album));
        }
    }
}

TEST_CASE("AC-08-5：FILE 指到資料夾外的 .cue 記進掃描報告（寫明原因），外面的檔案不被讀取") {
    Fixture fx(QStringLiteral("cue-confine-scan"));
    writeFlac(fx.dir + QStringLiteral("\\外面\\whole.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("外面的檔案")}});
    writeFlac(fx.root + QStringLiteral("\\單曲\\single.flac"), {});
    const QString evil = fx.root + QStringLiteral("\\專輯\\evil.cue");
    writeBytes(evil, cueText(QStringLiteral("..\\..\\外面\\whole.flac")));

    QStringList read;
    FaultGuard spy([&read](const QString& path) { read.append(path); });
    const ScanReport report = fx.scan();
    CHECK(report.added == 1);
    REQUIRE(report.failed == 1);
    CHECK(report.failures.front().path == evil);
    CHECK(report.failures.front().error.contains(QStringLiteral("資料夾以外")));
    for (const auto& path : read) {
        CHECK(under(path, fx.root));
    }
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM tracks")) == 1);
}

TEST_CASE("AC-08-5：要讀的區塊比剩下的檔案長時只讀到檔尾，不依宣告的長度配置記憶體") {
    const QString path = freshTestDir(QStringLiteral("read-clamp")) + QStringLiteral("\\a.bin");
    writeBytes(path, QByteArray(100, 'x'));
    ReadOnlyStream stream(path);
    REQUIRE(stream.isOpen());
    stream.seek(10);
    CHECK(stream.readBlock(ReadOnlyStream::kMaxReadBlock * 4).size() == 90);
    CHECK_FALSE(stream.readLimitExceeded());
}

TEST_CASE("AC-08-5：檔頭宣告超過讀取上限的檔案列為讀不到，掃描繼續") {
    Fixture fx(QStringLiteral("oversized"));
    const QString huge = fx.root + QStringLiteral("\\huge.mp3");
    writeOversizedId3(huge, static_cast<qint64>(ReadOnlyStream::kMaxReadBlock) + 1024 * 1024);
    writeFlac(fx.root + QStringLiteral("\\ok.flac"), {});

    const auto tags = readTags(huge);
    REQUIRE_FALSE(tags.hasValue());
    CHECK(tags.error().message.contains(QStringLiteral("上限")));

    const ScanReport report = fx.scan();
    CHECK(report.added == 1);
    REQUIRE(report.failed == 1);
    CHECK(report.failures.front().path == huge);
    QFile::remove(huge); // 70 MB 的暫存檔不留到下一輪
}

TEST_CASE("AC-08-5：讀標籤時丟出例外的檔案（含 CUE 的整軌檔）記進報告，掃描不中斷") {
    Fixture fx(QStringLiteral("throwing"));
    for (int i = 0; i < 12; ++i) {
        writeFlac(fx.root + QStringLiteral("\\正常\\%1.flac").arg(i), {});
    }
    const QString boom = fx.root + QStringLiteral("\\爆炸.flac");
    const QString oom = fx.root + QStringLiteral("\\記憶體.flac");
    writeFlac(boom, {});
    writeFlac(oom, {});
    writeFlac(fx.root + QStringLiteral("\\整軌\\爆炸整軌.flac"), {});
    const QString cue = fx.root + QStringLiteral("\\整軌\\album.cue");
    writeBytes(cue, cueText(QStringLiteral("爆炸整軌.flac")));

    FaultGuard fault([](const QString& path) {
        if (path.contains(QStringLiteral("爆炸"))) {
            throw std::runtime_error("模擬解析器丟出例外");
        }
        if (path.contains(QStringLiteral("記憶體"))) {
            throw std::bad_alloc();
        }
    });
    const ScanReport report = fx.scan();
    CHECK_FALSE(report.cancelled);
    CHECK(report.added == 12);
    CHECK(report.failed == 3);
    QStringList failed;
    for (const auto& failure : report.failures) {
        failed.append(failure.path);
        CHECK(failure.error.contains(QStringLiteral("例外")));
    }
    CHECK(failed.contains(boom));
    CHECK(failed.contains(oom));
    CHECK(failed.contains(cue));
}

TEST_CASE("AC-07-2：CUE 時間的分鐘超過 5 位數時視為格式錯誤（不做會溢位的換算）") {
    const auto ok = parseCueSheet(
        QStringLiteral("FILE \"a.flac\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 99999:59:74\n"));
    REQUIRE(ok.hasValue());
    CHECK(ok.value().tracks.front().start == (99999LL * 60 + 59) * 75 + 74);
    const auto bad = parseCueSheet(QStringLiteral(
        "FILE \"a.flac\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 999999999999999999:00:00\n"));
    CHECK_FALSE(bad.hasValue());
    const auto sixDigits = parseCueSheet(
        QStringLiteral("FILE \"a.flac\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 100000:00:00\n"));
    CHECK_FALSE(sixDigits.hasValue());
}

TEST_CASE("AC-08-5：超過 260 字元的路徑也讀得到標籤") {
    const QString dir = freshTestDir(QStringLiteral("long-path"));
    QString folder = dir;
    while (folder.size() < 280) {
        folder += QStringLiteral("\\很長的資料夾名稱很長的資料夾名稱很長的資料夾名稱abcdefghij");
    }
    const QString shortPath = dir + QStringLiteral("\\a.flac");
    writeFlac(shortPath, {{QStringLiteral("TITLE"), QStringLiteral("長路徑")}});
    REQUIRE(QDir().mkpath(folder));
    const QString longPath = folder + QStringLiteral("\\a.flac");
    REQUIRE(QFile::rename(shortPath, longPath));
    REQUIRE(longPath.size() > 260);
    const auto tags = readTags(longPath);
    REQUIRE(tags.hasValue());
    CHECK(tags.value().title == QStringLiteral("長路徑"));
}
