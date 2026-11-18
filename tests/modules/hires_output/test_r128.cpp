// R128 計算（T-38，AC-06-3、規格 §1 情況 5）：EBU R128 量測、結果只寫 rg_computed、專輯增益要整張
// 都算完才寫、中途取消已算完的保留、單首失敗與工作失敗只標記、DSD 不算。
#include "engine_helpers.h"
#include "r128.h"
#include "r128_controller.h"
#include "replay_gain.h"
#include "replay_gain_processor.h"

#include "library/migrator.h"
#include "library/module_database_impl.h"

#include <auralis/job_runner.h>
#include <auralis/library_query.h>
#include <auralis/track_drag_payload.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <numbers>

using auralis::AudioFormat;
using auralis::Error;
using auralis::IAudioSource;
using auralis::JobRunner;
using auralis::Result;
using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::core::ModuleDatabaseImpl;
using namespace auralis::mod::out;
using namespace auralis::test;

namespace {

void ensureApp() {
    static int argc = 1;
    static char name[] = "auralis_out_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QCoreApplication(argc, argv);
    }
}

constexpr quint32 kRate = 48000;
constexpr int kSeconds = 3;

/// 1 kHz 立體聲正弦，峰值 dbfs（EBU Tech 3341 第 1 項：-23 dBFS 量到 -23 LUFS）。
/// isFloat 時是 32 位元浮點，否則是 24-in-32 整數。
QByteArray sine(double dbfs, bool isFloat) {
    const double amplitude = std::pow(10.0, dbfs / 20.0);
    const qint64 frames = static_cast<qint64>(kRate) * kSeconds;
    QByteArray data(frames * 8, '\0');
    for (qint64 i = 0; i < frames; ++i) {
        const double value = amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * i / kRate);
        for (int channel = 0; channel < 2; ++channel) {
            char* at = data.data() + (i * 2 + channel) * 4;
            if (isFloat) {
                const auto sample = static_cast<float>(value);
                std::memcpy(at, &sample, 4);
            } else {
                // 24 位元左對齊在 32 位元容器
                const auto sample = static_cast<qint32>(std::lround(value * 8388607.0)) * 256;
                std::memcpy(at, &sample, 4);
            }
        }
    }
    return data;
}

AudioFormat formatOf(bool isFloat) {
    return isFloat ? AudioFormat{kRate, 32, 32, 2, true, 0}
                   : AudioFormat{kRate, 24, 32, 2, false, 0};
}

/// 路徑 → 訊號（dBFS、是否浮點）；記錄開過哪些檔。
struct Signals {
    QHash<QString, std::pair<double, bool>> byPath;
    QStringList failing;
    R128Opener opener() {
        return [this](const TrackRef& track) -> Result<std::unique_ptr<IAudioSource>> {
            const QString& path = track.path;
            if (failing.contains(path)) {
                return Error{QStringLiteral("檔案損毀"), 0, QStringLiteral("test")};
            }
            const auto [dbfs, isFloat] = byPath.value(path);
            return std::unique_ptr<IAudioSource>(
                std::make_unique<MemorySource>(formatOf(isFloat), sine(dbfs, isFloat)));
        };
    }
};

struct TestDatabase {
    QTemporaryDir dir{QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/r128-XXXXXX")};
    std::unique_ptr<ModuleDatabaseImpl> db;
    TestDatabase() {
        REQUIRE(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("library.db"));
        REQUIRE(auralis::core::migrateDatabase(path).hasValue());
        db = std::make_unique<ModuleDatabaseImpl>(QStringLiteral("OUT"), path);
    }
    /// path 那一列的 track_gain、track_peak、album_gain、album_peak（沒有這列時是空的）。
    QVariantList row(const QString& path) {
        auto rows = db->query(QStringLiteral("SELECT track_gain, track_peak, album_gain, "
                                             "album_peak, algorithm FROM rg_computed WHERE "
                                             "path_key = ? AND subsong = 0"),
                              {auralis::pathKey(path)});
        REQUIRE(rows.hasValue());
        return rows.value().isEmpty() ? QVariantList{} : rows.value().front();
    }
};

TrackInfo info(const QString& path, qint64 albumId, int dsdRate = 0) {
    TrackInfo track;
    track.ref = {path, 0};
    track.albumId = albumId;
    track.dsdRate = dsdRate;
    return track;
}

/// 假的音樂庫：findByIdentity 與 tracks(albumId) 從 all 取。
class FakeLibrary final : public auralis::LibraryQuery {
public:
    Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                    quint32 subsong) const override {
        if (failLookup) {
            return Error{QStringLiteral("資料庫被鎖住"), 0, QStringLiteral("test")};
        }
        for (const TrackInfo& track : all) {
            if (auralis::pathKey(track.ref.path) == auralis::pathKey(path) &&
                track.ref.subsong == subsong) {
                return std::optional<TrackInfo>(track);
            }
        }
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const auralis::TrackFilter& filter) const override {
        QList<TrackInfo> result;
        for (const TrackInfo& track : all) {
            if (filter.albumId == 0 || track.albumId == filter.albumId) {
                result.append(track);
            }
        }
        return result;
    }
    Result<QList<auralis::AlbumInfo>> albums() const override {
        return QList<auralis::AlbumInfo>{};
    }
    Result<QStringList> artists() const override { return QStringList{}; }
    Result<QList<auralis::FolderInfo>> folders(qint64) const override {
        return QList<auralis::FolderInfo>{};
    }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    QList<TrackInfo> all;
    bool failLookup = false;
};

const QString kA1 = QStringLiteral("D:\\音樂\\A\\01.flac");
const QString kA2 = QStringLiteral("D:\\音樂\\A\\02.flac");
const QString kB1 = QStringLiteral("D:\\音樂\\B\\01.flac");
const QString kDsd = QStringLiteral("D:\\音樂\\C\\01.dsf");

/// A 專輯兩首（-23、-13 dBFS）、B 專輯一首（-20 dBFS），加一首 DSD。
void fillLibrary(FakeLibrary& library, Signals& sources) {
    library.all = {info(kA1, 1), info(kA2, 1), info(kB1, 2), info(kDsd, 3, 2822400)};
    sources.byPath = {{kA1, {-23.0, false}}, {kA2, {-13.0, true}}, {kB1, {-20.0, false}}};
}

bool never() {
    return false;
}

void ignore(double) {}

/// 等控制器的背景工作結束（佇列的訊號要事件迴圈送）。
bool waitIdle(R128Controller& controller, int timeoutMs = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (controller.running() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return !controller.running();
}

} // namespace

TEST_CASE("AC-06-3：EBU R128 量測（-23 dBFS 正弦量到 -23 LUFS），音軌增益與峰值寫進 rg_computed") {
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    auto plan = planR128(library, {{kA1, 0}, {kA2, 0}});
    REQUIRE(plan.hasValue());
    const R128Summary summary = runR128(plan.value(), sources.opener(), *db.db, never, ignore);
    CHECK(summary.computed == 2);
    CHECK(summary.albums == 1);

    const QVariantList a1 = db.row(kA1);
    REQUIRE(a1.size() == 5);
    // ReplayGain 2.0：參考 -18 LUFS，-23 LUFS 的曲目要 +5 dB
    CHECK(a1[0].toDouble() == Catch::Approx(5.0).margin(0.1));
    CHECK(a1[1].toDouble() == Catch::Approx(std::pow(10.0, -23.0 / 20.0)).epsilon(0.01));
    CHECK(a1[4].toString() == QStringLiteral("ebur128"));
    const QVariantList a2 = db.row(kA2);
    REQUIRE(a2.size() == 5);
    CHECK(a2[0].toDouble() == Catch::Approx(-5.0).margin(0.1));
    // 專輯增益：兩首等長，整張的整合響度是能量平均
    const double albumLufs = 10.0 * std::log10((std::pow(10.0, -2.3) + std::pow(10.0, -1.3)) / 2.0);
    CHECK(a1[2].toDouble() == Catch::Approx(-18.0 - albumLufs).margin(0.1));
    CHECK(a2[2].toDouble() == Catch::Approx(a1[2].toDouble()));
    CHECK(a1[3].toDouble() == Catch::Approx(std::pow(10.0, -13.0 / 20.0)).epsilon(0.01));
}

TEST_CASE("AC-06-3：算好的值經 ReplayGain 的查法優先於標籤（從下一首起作用）") {
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    library.all[1].rgTrackGain = -1.0; // A2 的標籤
    auto plan = planR128(library, {{kA2, 0}});
    REQUIRE(plan.hasValue());
    runR128(plan.value(), sources.opener(), *db.db, never, ignore);
    const RgTrackGain gain = libraryLookup(*db.db, library)({kA2, 0});
    CHECK(gain.track.source == RgSource::Computed);
    CHECK(*gain.track.gainDb == Catch::Approx(-5.0).margin(0.1));
}

TEST_CASE("專輯增益：選取的曲目不是整張時只算音軌增益；DSD 不算並計數；重複的只算一次") {
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    auto partial = planR128(library, {{kA1, 0}, {kA1, 0}, {kB1, 0}, {kDsd, 0}});
    REQUIRE(partial.hasValue());
    CHECK(partial.value().items.size() == 2);
    CHECK_FALSE(partial.value().completeAlbums.contains(1));
    // B 只有一首，選了就是整張
    CHECK(partial.value().completeAlbums.contains(2));
    CHECK(partial.value().skippedDsd == 1);

    TestDatabase db;
    runR128(partial.value(), sources.opener(), *db.db, never, ignore);
    const QVariantList a1 = db.row(kA1);
    REQUIRE(a1.size() == 5);
    CHECK_FALSE(a1[0].isNull());
    CHECK(a1[2].isNull());
    CHECK(db.row(kDsd).isEmpty());

    // 整個音樂庫：每張專輯都完整，曲目依專輯排在一起
    auto whole = planLibraryR128(library);
    REQUIRE(whole.hasValue());
    CHECK(whole.value().items.size() == 3);
    CHECK(whole.value().completeAlbums == QSet<qint64>{1, 2});
    CHECK(whole.value().skippedDsd == 1);
}

TEST_CASE("情況 5：中途取消時已算完的保留，正在算的那首與那張的專輯增益不寫") {
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    auto plan = planLibraryR128(library);
    REQUIRE(plan.hasValue());
    // 之前算過整張 A：專輯值保留（同一份音訊），只有新算完的音軌值更新
    REQUIRE(db.db
                ->execute(QStringLiteral("INSERT INTO rg_computed (path_key, subsong, track_gain, "
                                         "album_gain, computed_at) VALUES (?, 0, 9.0, -3.0, 0)"),
                          {auralis::pathKey(kA1)})
                .hasValue());
    // 第一首量完、第二首量到一半時取消（每個區塊檢查一次）
    int checks = 0;
    const int firstTrackChecks = kRate * kSeconds / 4096 + 3;
    auto cancelled = [&checks, firstTrackChecks] {
        return ++checks > firstTrackChecks + 10;
    };
    const R128Summary summary = runR128(plan.value(), sources.opener(), *db.db, cancelled, ignore);
    CHECK(summary.cancelled);
    CHECK(summary.computed == 1);
    const QVariantList a1 = db.row(kA1);
    REQUIRE(a1.size() == 5);
    CHECK(a1[0].toDouble() == Catch::Approx(5.0).margin(0.1));
    CHECK(a1[2].toDouble() == -3.0);
    CHECK(db.row(kA2).isEmpty());
    CHECK(db.row(kB1).isEmpty());
}

TEST_CASE("錯誤邊界：單首開不了只略過那首，那張專輯不寫專輯增益") {
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    sources.failing = {kA2};
    auto plan = planLibraryR128(library);
    REQUIRE(plan.hasValue());
    const R128Summary summary = runR128(plan.value(), sources.opener(), *db.db, never, ignore);
    CHECK(summary.computed == 2);
    CHECK(summary.failed == 1);
    CHECK(summary.albums == 1); // 只有 B
    REQUIRE(summary.failures.size() == 1);
    CHECK(summary.failures.front().contains(QStringLiteral("檔案損毀")));
    CHECK(db.row(kA1)[2].isNull());
    CHECK_FALSE(db.row(kB1)[2].isNull());
    CHECK(r128SummaryText(summary) ==
          QStringLiteral("已計算 2 首（含 1 張專輯的專輯增益），1 首無法計算，"
                         "DSD 曲目 1 首不計算（DSD 播放時 ReplayGain 不作用）"));
}

TEST_CASE("背景工作：選取的曲目經 JobRunner 計算，完成後說明結果；工作失敗只標記這個工作") {
    ensureApp();
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    JobRunner jobs;
    R128Controller controller(jobs, library, *db.db, sources.opener(), nullptr);
    CHECK_FALSE(controller.running());

    auralis::TrackDragPayload payload;
    payload.tracks = {{kB1, 0}};
    controller.computeTracks(QString::fromUtf8(payload.toJson()));
    CHECK(controller.running());
    CHECK(controller.status() == QStringLiteral("正在計算 ReplayGain…"));
    REQUIRE(waitIdle(controller));
    CHECK(controller.status() == QStringLiteral("已計算 1 首（含 1 張專輯的專輯增益）"));
    CHECK_FALSE(db.row(kB1).isEmpty());

    // 查音樂庫失敗（例外在工作的錯誤邊界接住）：只有這個工作失敗，可以再算
    library.failLookup = true;
    controller.computeTracks(QString::fromUtf8(payload.toJson()));
    REQUIRE(waitIdle(controller));
    CHECK(controller.status().startsWith(QStringLiteral("ReplayGain 計算失敗（資料庫被鎖住）")));
    library.failLookup = false;
    controller.computeLibrary();
    REQUIRE(waitIdle(controller));
    CHECK(controller.status().startsWith(QStringLiteral("已計算 3 首")));
}

TEST_CASE("情況 5：程式關閉（模組停止）時取消並等工作停下，之後不再寫資料庫") {
    ensureApp();
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    // 很多首：關閉時一定還沒算完
    for (int i = 0; i < 200; ++i) {
        const QString path = QStringLiteral("D:\\音樂\\大\\%1.flac").arg(i);
        library.all.append(info(path, 9));
        sources.byPath.insert(path, {-20.0, false});
    }
    JobRunner jobs;
    R128Controller controller(jobs, library, *db.db, sources.opener(), nullptr);
    controller.computeLibrary();
    controller.shutdown();
    auto count = [&db] {
        auto rows =
            db.db->query(QStringLiteral("SELECT COUNT(*), COUNT(album_gain) FROM rg_computed"));
        REQUIRE(rows.hasValue());
        return rows.value().front();
    };
    const QVariantList after = count();
    CHECK(after[0].toInt() < 200);
    // 整張沒算完：專輯值一筆都沒有
    CHECK(after[1].toInt() == 0);
    REQUIRE(jobs.waitForDone(10000));
    CHECK(count() == after);
}

QStringList g_warnings;
void keepWarning(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type == QtWarningMsg && QLatin1String(context.category) == "auralis.out.r128") {
        g_warnings.append(message);
    }
}

TEST_CASE("錯誤邊界：無法計算的曲目與原因寫進日誌（WARN），畫面只寫首數") {
    ensureApp();
    TestDatabase db;
    FakeLibrary library;
    Signals sources;
    fillLibrary(library, sources);
    sources.failing = {kA2};
    JobRunner jobs;
    R128Controller controller(jobs, library, *db.db, sources.opener(), nullptr);
    g_warnings.clear();
    const QtMessageHandler previous = qInstallMessageHandler(keepWarning);
    controller.computeLibrary();
    const bool idle = waitIdle(controller);
    qInstallMessageHandler(previous);
    REQUIRE(idle);
    CHECK(controller.status().contains(QStringLiteral("1 首無法計算")));
    REQUIRE(g_warnings.size() == 1);
    CHECK(g_warnings.front().contains(kA2));
    CHECK(g_warnings.front().contains(QStringLiteral("檔案損毀")));
}
