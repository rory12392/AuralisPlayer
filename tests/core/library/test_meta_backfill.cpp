// 位元率與加入時間（AC-10-5）：掃描時寫入、升級後的背景補值（中斷續跑）、LibraryQuery 的排序。
#include "../audio/signal_writer.h"
#include "cue_fixtures.h"
#include "scan_fixture.h"
#include "test_qt_app.h"

#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/library_scanner.h"
#include "library/meta_backfill.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library/stream_bitrate.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

void ensureApp() {
    auralis::test::ensureQtApp("auralis_core_tests");
}

struct Library : ScanFixture {
    std::unique_ptr<LibraryQueryImpl> query;

    explicit Library(const QString& name)
        : ScanFixture(name), query(std::make_unique<LibraryQueryImpl>(databasePath)) {}

    /// 回到「v1.0 資料庫剛升級到 0002」的狀態：兩欄 NULL、全部待補。
    void resetToUpgraded() {
        const auto reset = db->execute(
            QStringLiteral("UPDATE tracks SET bitrate = NULL, added_at = NULL, meta_backfill = 1"));
        REQUIRE(reset.hasValue());
    }

    TrackInfo track(const QString& path, quint32 subsong = 0) {
        const auto found = query->findByIdentity(path, subsong);
        REQUIRE(found.hasValue());
        REQUIRE(found.value().has_value());
        return *found.value();
    }

    QList<TrackInfo> sorted(TrackSort sort, bool descending) {
        TrackFilter filter;
        filter.sort = sort;
        filter.descending = descending;
        auto found = query->tracks(filter);
        REQUIRE(found.hasValue());
        return found.value();
    }
};

/// 0.5 秒 1 kHz 正弦的 AAC（.m4a，有損）。
void writeAac(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    AudioFormat format;
    format.rate = 44100;
    format.bits = 32;
    format.container = 32;
    format.channels = 2;
    format.isFloat = true;
    const int frames = 22050;
    QByteArray signal(frames * 2 * 4, Qt::Uninitialized);
    auto* samples = reinterpret_cast<float*>(signal.data());
    for (int i = 0; i < frames; ++i) {
        samples[2 * i] = samples[2 * i + 1] =
            0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / 44100));
    }
    const QString why = encodeFile(path, "aac", "ipod", format, signal);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
}

qint64 bornMs(const QString& path) {
    return QFileInfo(path).birthTime().toMSecsSinceEpoch();
}

int sizeRate(const QString& path, qint64 durationMs) {
    return static_cast<int>(QFileInfo(path).size() * 8 / durationMs);
}

/// 無損（FLAC 0.1 秒）、有損（AAC）、CUE（5 秒整軌 FLAC 三軌）各一，加幾首一般的 FLAC。
struct Sample {
    QString flac, aac, cue, cueAudio;
    QStringList extras;

    explicit Sample(const QString& root, int extraCount = 0) {
        flac = root + QStringLiteral("\\無損\\a.flac");
        writeFlac(flac, {{QStringLiteral("TITLE"), QStringLiteral("無損")}});
        aac = root + QStringLiteral("\\有損\\b.m4a");
        writeAac(aac);
        cueAudio = root + QStringLiteral("\\整軌\\album.flac");
        writeFlac(cueAudio, {}, 44100, 16, 44100 * kSampleAudioSeconds);
        cue = root + QStringLiteral("\\整軌\\album.cue");
        writeBytes(cue, cueFixture(QStringLiteral("utf8-bom.cue")));
        for (int i = 0; i < extraCount; ++i) {
            extras.append(root + QStringLiteral("\\其他\\%1.flac").arg(i));
            writeFlac(extras.last(), {});
        }
    }
};

/// 補值（或掃描）後各類曲目的值都對。
void checkFilled(Library& lib, const Sample& sample, bool scanTime) {
    const TrackInfo flac = lib.track(sample.flac);
    REQUIRE(flac.durationMs == 100);
    CHECK(flac.bitrate == sizeRate(sample.flac, 100));

    const TrackInfo aac = lib.track(sample.aac);
    const auto reported = ffmpegBitrateKbps(sample.aac);
    REQUIRE(reported.has_value());
    CHECK(aac.bitrate == reported);
    CHECK(*reported >= 16);
    CHECK(*reported <= 512);

    const int wholeFile = sizeRate(sample.cueAudio, kSampleAudioSeconds * 1000);
    for (quint32 subsong = 1; subsong <= 3; ++subsong) {
        INFO("CUE 第 " << subsong << " 軌");
        const TrackInfo cueTrack = lib.track(sample.cue, subsong);
        CHECK(cueTrack.bitrate == wholeFile); // 整軌檔整體的值，不是這一段的
        if (!scanTime) {
            CHECK(cueTrack.addedAt == bornMs(sample.cue));
        }
    }
    if (!scanTime) {
        CHECK(flac.addedAt == bornMs(sample.flac));
        CHECK(aac.addedAt == bornMs(sample.aac));
    }
}

} // namespace

TEST_CASE("AC-10-5：位元率規則：有損用 FFmpeg 回報值，無損用大小 × 8 ÷ 長度，長度 0 時未知") {
    CHECK(audioFileBitrateKbps(QStringLiteral("x.flac"), QStringLiteral("FLAC"), 1000, 0) ==
          std::nullopt);
    CHECK(audioFileBitrateKbps(QStringLiteral("x.flac"), QStringLiteral("FLAC"), 0, 1000) ==
          std::nullopt);
    // 1 MB、10 秒 → 838 kbps（整數除法）
    CHECK(audioFileBitrateKbps(QStringLiteral("x.wav"), QStringLiteral("WAV"), 1048576, 10000) ==
          838);
    CHECK(isLossyCodec(QStringLiteral("MP3")));
    CHECK(isLossyCodec(QStringLiteral("Opus")));
    CHECK_FALSE(isLossyCodec(QStringLiteral("ALAC")));
    CHECK_FALSE(isLossyCodec(QStringLiteral("DSD")));
    // 有損檔讀不到時是未知，不退回用大小換算
    CHECK(audioFileBitrateKbps(QStringLiteral("Z:\\不存在\\x.mp3"), QStringLiteral("MP3"), 1000,
                               1000) == std::nullopt);
}

TEST_CASE("AC-10-5：新掃描的曲目寫入位元率與加入時間（掃描時間），重新掃描不覆寫加入時間") {
    Library lib(QStringLiteral("t-114-scan"));
    const Sample sample(lib.root);
    const QString empty = lib.root + QStringLiteral("\\無損\\空的.wav");
    writeWav(empty, {}, 96000, 0);

    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    REQUIRE(lib.scan().failed == 0);
    const qint64 after = QDateTime::currentMSecsSinceEpoch();
    checkFilled(lib, sample, true);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM tracks WHERE meta_backfill <> 0")) == 0);
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM tracks WHERE added_at < %1 OR "
                                   "added_at > %2 OR added_at IS NULL")
                        .arg(before)
                        .arg(after)) == 0);
    // 長度 0 的曲目位元率是未知（NULL），不是 0 或除以零
    const TrackInfo silent = lib.track(empty);
    CHECK(silent.durationMs == 0);
    CHECK_FALSE(silent.bitrate.has_value());

    // 檔案改過（長度變成 0.2 秒）：重新讀標籤、位元率跟著更新，加入時間不變
    const qint64 addedAt = *lib.track(sample.flac).addedAt;
    writeFlac(sample.flac, {{QStringLiteral("TITLE"), QStringLiteral("無損")}}, 44100, 16, 8820);
    QFile file(sample.flac);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(QDateTime::currentDateTime().addSecs(5),
                             QFileDevice::FileModificationTime));
    file.close();
    REQUIRE(lib.scan().updated == 1);
    const TrackInfo changed = lib.track(sample.flac);
    CHECK(changed.durationMs == 200);
    CHECK(changed.bitrate == sizeRate(sample.flac, 200));
    CHECK(changed.addedAt == addedAt);
}

TEST_CASE("AC-10-5：升級後的背景補值把無損、有損、CUE 的位元率與加入時間補齊，補完不再是待補") {
    Library lib(QStringLiteral("t-114-backfill"));
    const Sample sample(lib.root);
    REQUIRE(lib.scan().failed == 0);
    lib.resetToUpgraded();
    // 補值期間兩欄是空白
    CHECK_FALSE(lib.track(sample.flac).bitrate.has_value());
    CHECK_FALSE(lib.track(sample.flac).addedAt.has_value());

    const BackfillResult result = runMetaBackfill(*lib.db);
    CHECK(result.total == 5);
    CHECK(result.updated == 5);
    CHECK(result.deferred == 0);
    CHECK_FALSE(result.cancelled);
    checkFilled(lib, sample, false);
    CHECK(pendingBackfillCount(*lib.db) == 0);
    // 沒有待補時什麼都不做
    CHECK(runMetaBackfill(*lib.db).updated == 0);
}

TEST_CASE("AC-10-5：補值中斷後再跑一次接著補完，不重複、不漏；檔案不在的留到下次") {
    Library lib(QStringLiteral("t-114-resume"));
    const Sample sample(lib.root, 7);
    REQUIRE(lib.scan().failed == 0);
    lib.resetToUpgraded();
    const int total = 12; // 無損、有損各 1 首＋CUE 3 軌＋其他 7 首（整軌檔由 CUE 涵蓋，不另列）
    REQUIRE(pendingBackfillCount(*lib.db) == total);

    // 第一次：處理 5 首後停止（跨兩批，每批 4 首）
    int calls = 0;
    int processed = 0;
    BackfillCallbacks stopEarly;
    stopEarly.cancelled = [&calls] {
        return ++calls > 5;
    };
    stopEarly.progress = [&processed](int, int) {
        ++processed;
    };
    const BackfillResult first = runMetaBackfill(*lib.db, stopEarly, 4);
    CHECK(first.cancelled);
    CHECK(first.updated == 5);
    CHECK(processed == 5);
    CHECK(pendingBackfillCount(*lib.db) == total - 5);

    // 第二次（重新啟動）：只處理剩下的
    int processedAgain = 0;
    BackfillCallbacks resume;
    resume.progress = [&processedAgain](int, int) {
        ++processedAgain;
    };
    const BackfillResult second = runMetaBackfill(*lib.db, resume, 4);
    CHECK(second.total == total - 5);
    CHECK(second.updated == total - 5);
    CHECK(processedAgain == total - 5);
    CHECK(pendingBackfillCount(*lib.db) == 0);
    checkFilled(lib, sample, false);
    for (const QString& extra : sample.extras) {
        CHECK(lib.track(extra).addedAt == bornMs(extra));
    }

    // 檔案暫時不在（磁碟離線）：那一首維持待補，其他照補
    lib.resetToUpgraded();
    const QString moved = sample.extras.first() + QStringLiteral(".moved");
    REQUIRE(QFile::rename(sample.extras.first(), moved));
    const BackfillResult offline = runMetaBackfill(*lib.db);
    CHECK(offline.deferred == 1);
    CHECK(offline.updated == total - 1);
    CHECK(pendingBackfillCount(*lib.db) == 1);
    REQUIRE(QFile::rename(moved, sample.extras.first()));
    CHECK(runMetaBackfill(*lib.db).updated == 1);
}

TEST_CASE("AC-10-5：CUE 在、它指向的音訊檔暫時不在時各軌維持待補，音訊檔回來後補上位元率") {
    Library lib(QStringLiteral("t-125-cue-offline"));
    const Sample sample(lib.root);
    REQUIRE(lib.scan().failed == 0);
    lib.resetToUpgraded();
    const QString moved = sample.cueAudio + QStringLiteral(".moved");
    REQUIRE(QFile::rename(sample.cueAudio, moved));

    const BackfillResult offline = runMetaBackfill(*lib.db);
    CHECK(offline.deferred == 3);
    CHECK(offline.updated == 2);
    CHECK(pendingBackfillCount(*lib.db) == 3);
    CHECK_FALSE(lib.track(sample.cue, 1).bitrate.has_value());

    REQUIRE(QFile::rename(moved, sample.cueAudio));
    const BackfillResult back = runMetaBackfill(*lib.db);
    CHECK(back.updated == 3);
    CHECK(back.deferred == 0);
    CHECK(pendingBackfillCount(*lib.db) == 0);
    checkFilled(lib, sample, false);
}

TEST_CASE("AC-10-5：補值在背景工作裡做；停止服務（解構）時關閘門等它結束，重開後接著補完") {
    ensureApp();
    Library lib(QStringLiteral("t-114-service"));
    const Sample sample(lib.root, 20);
    REQUIRE(lib.scan().failed == 0);
    lib.resetToUpgraded();
    const int total = static_cast<int>(pendingBackfillCount(*lib.db));
    {
        // 啟動後立刻停止服務：工作可能還沒開始、做到一半或做完，都不能當機或寫壞資料
        JobRunner jobs;
        LibraryQueryImpl query{lib.databasePath};
        LibraryManager manager{jobs, query, lib.databasePath};
        manager.startMetaBackfill();
    }
    const int left = pendingBackfillCount(*lib.db);
    CHECK(left <= total);

    JobRunner jobs;
    LibraryQueryImpl query{lib.databasePath};
    LibraryManager manager{jobs, query, lib.databasePath};
    bool notified = false;
    QObject::connect(&query, &LibraryQuery::scanFinished, [&notified] { notified = true; });
    QString name;
    QObject::connect(&jobs, &JobRunner::submitted,
                     [&name](quint64, const QString& jobName) { name = jobName; });
    manager.startMetaBackfill();
    CHECK(name == QStringLiteral("補齊位元率與加入時間"));
    REQUIRE(QTest::qWaitFor([&lib] { return pendingBackfillCount(*lib.db) == 0; }, 30000));
    if (left > 0) {
        CHECK(QTest::qWaitFor([&notified] { return notified; }, 5000));
    }
    checkFilled(lib, sample, false);
}

TEST_CASE("AC-10-5：LibraryQuery 依位元率、加入時間排序，沒有值的不論升降冪都排最後") {
    Library lib(QStringLiteral("t-114-sort"));
    QStringList paths;
    for (int i = 0; i < 4; ++i) {
        paths.append(lib.root + QStringLiteral("\\%1.flac").arg(i));
        writeFlac(paths.last(), {{QStringLiteral("TITLE"), QString::number(i)}});
    }
    REQUIRE(lib.scan().failed == 0);
    // 0：320 kbps／最新，1：未知，2：1411／最舊，3：128／加入時間未知
    const QList<std::pair<QVariant, QVariant>> values{
        {320, 3000}, {QVariant(), 2000}, {1411, 1000}, {128, QVariant()}};
    const QString update = QStringLiteral("UPDATE tracks SET bitrate = ?, added_at = ? "
                                          "WHERE file_id = (SELECT id FROM files WHERE path = ?)");
    for (int i = 0; i < 4; ++i) {
        const auto set = lib.db->execute(update, {values[i].first, values[i].second, paths[i]});
        REQUIRE(set.hasValue());
    }
    const auto titles = [](const QList<TrackInfo>& tracks) {
        QStringList result;
        for (const auto& track : tracks) {
            result.append(track.title);
        }
        return result;
    };
    CHECK(titles(lib.sorted(TrackSort::Bitrate, false)) == QStringList{"3", "0", "2", "1"});
    CHECK(titles(lib.sorted(TrackSort::Bitrate, true)) == QStringList{"2", "0", "3", "1"});
    CHECK(titles(lib.sorted(TrackSort::AddedAt, false)) == QStringList{"2", "1", "0", "3"});
    CHECK(titles(lib.sorted(TrackSort::AddedAt, true)) == QStringList{"0", "1", "2", "3"});
    CHECK(lib.track(paths[0]).bitrate == 320);
    CHECK(lib.track(paths[0]).addedAt == 3000);
    CHECK_FALSE(lib.track(paths[1]).bitrate.has_value());
}
