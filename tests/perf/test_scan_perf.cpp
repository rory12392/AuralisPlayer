// 掃描效能（AC-08-2）：首次掃描與沒有變動時的增量檢查。
// 預設在測試暫存產生 2,000 個小檔案做小規模量測，門檻依檔案數從 4 萬首的目標等比例換算
// （首次 5 分鐘、增量 10 秒）。里程碑的正式量測用環境變數指定：
//   AURALIS_PERF_LIBRARY  測試音樂庫的資料夾（不存在時在這裡產生；只用 .localdata 底下的測試複本）
//   AURALIS_PERF_FILES    產生的檔案數（預設 2000；正式量測 40000）
//   AURALIS_PERF_RUNS     量測次數（預設 5，取中位數；另外先暖機一次）
#include "library/library_scanner.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace auralis::core;
using namespace auralis::test;

namespace {

constexpr double kTargetFiles = 40000.0;
constexpr double kFirstScanTargetMs = 5 * 60 * 1000.0;
constexpr double kIncrementalTargetMs = 10 * 1000.0;

int envInt(const char* name, int fallback) {
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? fallback : value.toInt();
}

/// 每個資料夾 50 首、兩層資料夾，內容是同一個 0.1 秒的 FLAC（有完整標籤）。
/// AURALIS_PERF_LIBRARY 指到已存在的資料夾時只讀它、不產生也不刪除任何東西。
QString prepareLibrary(int files) {
    QString root = QString::fromLocal8Bit(qgetenv("AURALIS_PERF_LIBRARY"));
    if (!root.isEmpty() && QFileInfo::exists(root)) {
        return QDir::toNativeSeparators(root);
    }
    if (root.isEmpty()) {
        root = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-09/perf-%1").arg(files);
    }
    root = QDir::toNativeSeparators(root);
    const QString marker = root + QStringLiteral("\\.complete-%1").arg(files);
    if (QFile::exists(marker)) {
        return root; // 上一次產生好的，直接用
    }
    // 只會刪到測試暫存裡沒產生完的那一份（指定的資料夾不存在才會走到產生這一步）
    QDir(root).removeRecursively();
    const QString source = root + QStringLiteral("\\source.flac.tmp");
    writeFlac(source, {{QStringLiteral("TITLE"), QStringLiteral("效能測試曲")},
                       {QStringLiteral("ARTIST"), QStringLiteral("Perf Artist")},
                       {QStringLiteral("ALBUMARTIST"), QStringLiteral("Perf Artist")},
                       {QStringLiteral("ALBUM"), QStringLiteral("Perf Album")},
                       {QStringLiteral("DATE"), QStringLiteral("2026")},
                       {QStringLiteral("TRACKNUMBER"), QStringLiteral("1")}});
    for (int i = 0; i < files; ++i) {
        const QString folder =
            root + QStringLiteral("\\藝人 %1\\專輯 %2").arg(i / 1000).arg(i / 50 % 20);
        QDir().mkpath(folder);
        REQUIRE(QFile::copy(
            source, folder + QStringLiteral("\\%1 曲目.flac").arg(i, 5, 10, QLatin1Char('0'))));
    }
    QFile::remove(source);
    writeBytes(marker, {});
    return root;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

} // namespace

TEST_CASE("AC-08-2：首次掃描與沒有變動時的增量檢查在門檻內（依檔案數換算）") {
    int files = envInt("AURALIS_PERF_FILES", 2000);
    const int runs = std::max(1, envInt("AURALIS_PERF_RUNS", 5));
    const QString root = prepareLibrary(files);
    const QString dbDir = freshTestDir(QStringLiteral("perf-db"));

    std::vector<double> firstScans;
    std::vector<double> incrementals;
    for (int run = 0; run <= runs; ++run) { // 第 0 次是暖機，不計
        const QString dbPath = dbDir + QStringLiteral("\\library-%1.db").arg(run);
        REQUIRE(migrateDatabase(dbPath).hasValue());
        auto db = std::move(SqliteConnection::open(dbPath)).value();
        LibraryWriter writer(*db);
        const QList<LibraryRoot> roots{{writer.ensureRoot(root), root}};

        QElapsedTimer timer;
        timer.start();
        const ScanReport first = scanLibrary(*db, roots, {});
        const double firstMs = static_cast<double>(timer.elapsed());
        files = first.scanned; // 指定的既有音樂庫時以實際檔案數為準
        REQUIRE(first.added + first.failed == files);

        timer.restart();
        const ScanReport again = scanLibrary(*db, roots, {});
        const double incrementalMs = static_cast<double>(timer.elapsed());
        REQUIRE(again.scanned == files);
        REQUIRE(again.added + again.updated + again.removed == 0);
        if (run > 0) {
            firstScans.push_back(firstMs);
            incrementals.push_back(incrementalMs);
        }
    }

    const double scale = files / kTargetFiles;
    const double firstMedian = median(firstScans);
    const double incrementalMedian = median(incrementals);
    const QString summary =
        QStringLiteral("掃描效能：%1 首，首次 %2 ms（門檻 %3 ms，換算 4 萬首約 %4 秒），"
                       "增量 %5 ms（門檻 %6 ms），%7 次中位數，讀標籤 %8 執行緒")
            .arg(files)
            .arg(firstMedian)
            .arg(kFirstScanTargetMs * scale)
            .arg(firstMedian / scale / 1000.0, 0, 'f', 1)
            .arg(incrementalMedian)
            .arg(kIncrementalTargetMs * scale)
            .arg(runs)
            .arg(tagReaderThreads());
    std::printf("%s\n", summary.toUtf8().constData());
    INFO(summary.toStdString());
    CHECK(firstMedian <= kFirstScanTargetMs * scale);
    CHECK(incrementalMedian <= kIncrementalTargetMs * scale);
}
