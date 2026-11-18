// 搜尋效能（AC-09-1）：4 萬首的音樂庫，從最後一個按鍵到第一批結果放進清單
// ≤ 200 ms（含去抖 120 ms）。
// 音樂庫用 .localdata\testcopy\perf-40k（掃描效能測試產生的 4 萬首合成音樂庫，只讀）：
//   AURALIS_PERF_LIBRARY  指定別的資料夾（只用 .localdata 底下的測試複本）
//   AURALIS_PERF_RUNS     量測次數（預設 5，取中位數；另外先暖機一次）
// 第一次執行時把它掃進測試暫存的資料庫（約幾分鐘），之後沿用。
// 同一個資料庫也量資料夾頁（S-06，T-23）的樹與曲目讀取。
#include "folder_tree.h"
#include "lib_test_helpers.h"
#include "search_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace auralis;
using namespace auralis::test;
using auralis::mod::lib::SearchController;

namespace {

constexpr double kTargetMs = 200.0;
constexpr int kKeystrokeGapMs = 40; // 打字的間隔，比去抖短

QString perfLibrary() {
    QStringList candidates;
    const QString fromEnv = QString::fromLocal8Bit(qgetenv("AURALIS_PERF_LIBRARY"));
    if (!fromEnv.isEmpty()) {
        candidates.append(fromEnv);
    }
    // 主目錄與 worktree（.worktrees\<名稱>）都找得到主目錄的 .localdata\testcopy
    const QString source = QString::fromUtf8(AURALIS_SOURCE_DIR);
    candidates.append(source + QStringLiteral("/.localdata/testcopy/perf-40k"));
    candidates.append(source + QStringLiteral("/../../.localdata/testcopy/perf-40k"));
    QString found;
    for (const QString& path : candidates) {
        if (found.isEmpty() && QFileInfo(path).isDir()) {
            found = QDir::toNativeSeparators(QFileInfo(path).canonicalFilePath());
        }
    }
    INFO("找不到 4 萬首的測試音樂庫（.localdata\\testcopy\\perf-40k）：先跑掃描效能測試產生，"
         "或用 AURALIS_PERF_LIBRARY 指定");
    REQUIRE_FALSE(found.isEmpty());
    return found;
}

/// 掃好的資料庫；記號檔記著來源資料夾，換了資料夾就重掃。
QString perfDatabase() {
    const QString library = perfLibrary();
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-20/perf-search");
    const QString db = dir + QStringLiteral("/library.db");
    const QString marker = dir + QStringLiteral("/.complete");
    QFile markerFile(marker);
    if (markerFile.open(QIODevice::ReadOnly) &&
        QString::fromUtf8(markerFile.readAll()) == library) {
        return db;
    }
    markerFile.close();
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    std::printf("掃描 %s 到測試資料庫…\n", qPrintable(library));
    scanIntoDatabase(db, library);
    QFile done(marker);
    REQUIRE(done.open(QIODevice::WriteOnly));
    done.write(library.toUtf8());
    return db;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct Case {
    const char* name;
    QString text;
    int year = 0;
    QString format;
    int sampleRate = 0;
};

} // namespace

TEST_CASE("AC-09-1：4 萬首中輸入關鍵字後 200 毫秒內顯示結果（search，從最後一個按鍵算）") {
    ensureApp();
    const QString dbPath = perfDatabase();
    core::LibraryQueryImpl library(dbPath); // 只保存路徑的參考
    SearchController search(library);
    int ready = 0;
    QObject::connect(&search, &SearchController::resultsReady, [&] { ++ready; });
    search.refresh();
    REQUIRE(waitFor([&] { return search.complete() && ready == 1; }, 60000));
    const int total = search.count();
    std::printf("音樂庫 %d 首\n", total);
    CHECK(total >= 40000);

    const int measured = qEnvironmentVariableIsSet("AURALIS_PERF_RUNS")
                             ? std::max(1, qEnvironmentVariableIntValue("AURALIS_PERF_RUNS"))
                             : 5;
    const std::vector<Case> cases = {
        {"2 個字（LIKE），全部符合", QStringLiteral("效能")},
        {"兩個詞（FTS5），全部符合", QStringLiteral("perf artist")},
        {"沒有符合", QStringLiteral("zzzz")},
        {"1 個字加三個篩選", QStringLiteral("曲"), 2026, QStringLiteral("FLAC"), 44100},
    };
    for (const Case& c : cases) {
        search.setYear(c.year);
        search.setFormat(c.format);
        search.setSampleRate(c.sampleRate);
        std::vector<double> samples;
        for (int run = 0; run <= measured; ++run) {
            // 從空白開始一個字一個字打；上一次的結果都載入完（閒置）才開始
            search.setText({});
            REQUIRE(waitFor([&] { return !search.busy() && search.complete(); }, 60000));
            QElapsedTimer timer;
            int before = 0;
            for (qsizetype i = 1; i <= c.text.size(); ++i) {
                before = ready;
                search.setText(c.text.left(i));
                timer.start();
                if (i < c.text.size()) {
                    waitFor([] { return false; }, kKeystrokeGapMs);
                }
            }
            REQUIRE(waitFor([&] { return ready > before; }, 5000));
            const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            if (run > 0) { // 第 0 次是暖機
                samples.push_back(ms);
            }
        }
        const double m = median(samples);
        std::printf("%s：中位數 %.1f ms（查詢第一批 %d ms，%d 首符合）\n", c.name, m,
                    search.elapsedMs(), search.complete() ? search.count() : -1);
        INFO(c.name << "：中位數 " << m << " ms，門檻 " << kTargetMs << " ms");
        CHECK(m <= kTargetMs);
    }
}

// 資料夾頁（S-06，T-23）：4 萬首時打開資料夾樹（一層一層讀 folders 表＋檢查每個資料夾在不在）
// 與選資料夾讀曲目（含子資料夾）的耗時。工作卡：樹的開啟超過 200 ms 才排核心的 COUNT 查詢修正。
// 都在工作執行緒，這裡直接量函式本身。ctest 名稱含「folders」：-L perf -R folders
TEST_CASE("S-06 效能：4 萬首時資料夾樹 200 毫秒內開啟，選資料夾讀曲目（folders）") {
    ensureApp();
    const QString dbPath = perfDatabase();
    core::LibraryQueryImpl library(dbPath); // 只保存路徑的參考
    std::vector<double> treeMs;
    std::vector<double> rootMs;
    std::vector<double> leafMs;
    QList<mod::lib::FolderNode> nodes;
    for (int run = 0; run <= 5; ++run) {
        QElapsedTimer timer;
        timer.start();
        REQUIRE(mod::lib::readFolderTree(library, nodes, true).isEmpty());
        const double tree = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        REQUIRE_FALSE(nodes.isEmpty());
        QList<TrackInfo> tracks;
        timer.start();
        REQUIRE(mod::lib::readFolderTracks(library, mod::lib::subtreeFolderIds(nodes, 0), tracks)
                    .isEmpty());
        const double root = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        // 最後一個節點一定沒有子資料夾（前序的最後一個）
        timer.start();
        REQUIRE(mod::lib::readFolderTracks(
                    library, mod::lib::subtreeFolderIds(nodes, static_cast<int>(nodes.size()) - 1),
                    tracks)
                    .isEmpty());
        const double leaf = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        if (run > 0) { // 第 0 次是暖機
            treeMs.push_back(tree);
            rootMs.push_back(root);
            leafMs.push_back(leaf);
        }
    }
    const double tree = median(treeMs);
    std::printf("資料夾樹：%lld 個資料夾，開啟中位數 %.1f ms；選音樂資料夾（全部曲目）%.1f ms；"
                "選最底層的資料夾 %.1f ms\n",
                static_cast<long long>(nodes.size()), tree, median(rootMs), median(leafMs));
    INFO("資料夾樹開啟中位數 " << tree << " ms，門檻 " << kTargetMs << " ms");
    CHECK(tree <= kTargetMs);
}
