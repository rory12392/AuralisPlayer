// R128 計算 CUE 整軌的曲目（M-03 完整驗證的擋 1，AC-06-3、AC-07-2）：用真的 FLAC＋.cue 與核心的
// SourceRegistry::openTrack，三軌各自量到自己那一段的響度、寫進 rg_computed（.cue 路徑加軌號），
// 整軌檔與 .cue 都不被修改。
#include "r128.h"

#include "audio/source_registry_impl.h"
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "signal_writer.h"

#include <auralis/library_query.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <numbers>

using auralis::AudioFormat;
using auralis::TrackRef;
using auralis::core::ModuleDatabaseImpl;
using auralis::core::SourceRegistryImpl;
using namespace auralis::mod::out;

namespace {

constexpr quint32 kRate = 48000;
/// 三軌的起點（CD 影格，1/75 秒）與整軌檔長度；每一軌的 1 kHz 正弦峰值不同。
constexpr int kTrackStart[] = {0, 150, 270};
constexpr int kTotalCdFrames = 375; // 5 秒
constexpr double kTrackDbfs[] = {-23.0, -13.0, -30.0};

/// 16 位元立體聲：每一軌一段 1 kHz 正弦（EBU Tech 3341：-23 dBFS 量到 -23 LUFS）。
QByteArray albumPcm() {
    const qint64 frames = static_cast<qint64>(kTotalCdFrames) * kRate / 75;
    QByteArray data(frames * 4, '\0');
    for (qint64 i = 0; i < frames; ++i) {
        int track = 2;
        while (track > 0 && i < static_cast<qint64>(kTrackStart[track]) * kRate / 75) {
            --track;
        }
        const double amplitude = std::pow(10.0, kTrackDbfs[track] / 20.0);
        const double value = amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * i / kRate);
        const auto sample = static_cast<qint16>(std::lround(value * 32767.0));
        std::memcpy(data.data() + i * 4, &sample, 2);
        std::memcpy(data.data() + i * 4 + 2, &sample, 2);
    }
    return data;
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}

} // namespace

TEST_CASE("AC-06-3：CUE 整軌的三軌經核心依曲目身分開音源，各自量到自己那一段、寫進 rg_computed") {
    QTemporaryDir dir{QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/r128-cue-XXXXXX")};
    REQUIRE(dir.isValid());
    const QString flac = QDir::toNativeSeparators(dir.filePath(QStringLiteral("整軌.flac")));
    const QString cue = QDir::toNativeSeparators(dir.filePath(QStringLiteral("整軌.cue")));
    const AudioFormat format{kRate, 16, 16, 2, false, 0};
    const QString why = auralis::test::encodeFile(flac, "flac", "flac", format, albumPcm());
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    writeFile(cue, QByteArrayLiteral("FILE \"\xE6\x95\xB4\xE8\xBB\x8C.flac\" WAVE\n"
                                     "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
                                     "  TRACK 02 AUDIO\n    INDEX 01 00:02:00\n"
                                     "  TRACK 03 AUDIO\n    INDEX 01 00:03:45\n"));
    const QDateTime flacTime = QFileInfo(flac).lastModified();
    const QDateTime cueTime = QFileInfo(cue).lastModified();

    const QString dbPath = dir.filePath(QStringLiteral("library.db"));
    REQUIRE(auralis::core::migrateDatabase(dbPath).hasValue());
    ModuleDatabaseImpl db(QStringLiteral("OUT"), dbPath);

    R128Plan plan;
    for (quint32 track = 1; track <= 3; ++track) {
        plan.items.append({TrackRef{cue, track}, 1});
    }
    plan.completeAlbums.insert(1);
    SourceRegistryImpl sources;
    const R128Summary summary = runR128(
        plan, [&sources](const TrackRef& track) { return sources.openTrack(track); }, db,
        [] { return false; }, [](double) {});
    INFO(summary.failures.join(QLatin1Char('\n')).toStdString());
    CHECK(summary.computed == 3);
    CHECK(summary.failed == 0);
    CHECK(summary.albums == 1);

    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        auto rows = db.query(QStringLiteral("SELECT track_gain, track_peak, album_gain FROM "
                                            "rg_computed WHERE path_key = ? AND subsong = ?"),
                             {auralis::pathKey(cue), i + 1});
        REQUIRE(rows.hasValue());
        REQUIRE(rows.value().size() == 1);
        const QVariantList& row = rows.value().front();
        // 增益 = -18 − 這一段的響度；範圍錯了（整個檔案或錯的區段）會混進別段的響度
        CHECK(row[0].toDouble() == Catch::Approx(kR128ReferenceLufs - kTrackDbfs[i]).margin(0.1));
        CHECK(row[1].toDouble() ==
              Catch::Approx(std::pow(10.0, kTrackDbfs[i] / 20.0)).epsilon(0.01));
        CHECK_FALSE(row[2].isNull());
    }
    // 只讀：整軌檔與 .cue 都沒有被改
    CHECK(QFileInfo(flac).lastModified() == flacTime);
    CHECK(QFileInfo(cue).lastModified() == cueTime);
}
