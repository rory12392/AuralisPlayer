// 掃描流程：唯讀（AC-08-4）、讀不到的檔案記進報告（AC-08-5）、增量、進度與取消（AC-08-1）。
#include "scan_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>

#include <filesystem>
#include <vector>

#include <windows.h>

using namespace auralis::core;
using namespace auralis::test;

namespace {

using Fixture = ScanFixture;

/// 每個檔案與資料夾：內容雜湊（資料夾為空）＋最後修改時間（100 奈秒精度）＋大小。
QMap<QString, QString> snapshot(const QString& root) {
    QMap<QString, QString> result;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const auto time = std::filesystem::last_write_time(path.toStdWString());
        QString entry = QString::number(time.time_since_epoch().count());
        if (it.fileInfo().isFile()) {
            QFile file(path);
            REQUIRE(file.open(QIODevice::ReadOnly));
            entry +=
                QLatin1Char(' ') +
                QString::fromLatin1(
                    QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()) +
                QLatin1Char(' ') + QString::number(it.fileInfo().size());
        }
        result.insert(path, entry);
    }
    return result;
}

void touchLater(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(QDateTime::currentDateTime().addSecs(120),
                             QFileDevice::FileModificationTime));
}

} // namespace

TEST_CASE("AC-08-4：掃描前後音樂檔與資料夾的雜湊與修改時間不變") {
    Fixture fx(QStringLiteral("readonly"));
    buildSampleLibrary(fx.root);
    const auto before = snapshot(fx.root);
    REQUIRE(before.size() >= 13);

    const ScanReport first = fx.scan();
    CHECK(first.added == 5);
    const ScanReport second = fx.scan(); // 增量（只重讀上次讀不到的那一個）
    CHECK(second.added == 0);

    const auto after = snapshot(fx.root);
    CHECK(after.keys() == before.keys()); // 沒有新增或刪除任何檔案
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        INFO(it.key().toStdString());
        CHECK(after.value(it.key()) == it.value());
    }
}

TEST_CASE("AC-08-5：讀不到的檔案（損毀、被鎖住）記在掃描報告，不中斷掃描") {
    Fixture fx(QStringLiteral("unreadable"));
    const int audioFiles = buildSampleLibrary(fx.root);
    const QString locked = fx.root + QStringLiteral("\\專輯 A\\鎖住.flac");
    writeFlac(locked, {});
    HANDLE handle = CreateFileW(locked.toStdWString().c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    const ScanReport report = fx.scan();
    CloseHandle(handle);

    CHECK(report.scanned == audioFiles + 1);
    CHECK(report.added == audioFiles - 1); // 損毀的那一個不算
    CHECK(report.failed == 2);
    QStringList failedPaths;
    for (const auto& failure : report.failures) {
        failedPaths.append(failure.path);
        CHECK_FALSE(failure.error.isEmpty());
    }
    CHECK(failedPaths.contains(locked));
    CHECK(failedPaths.contains(fx.root + QStringLiteral("\\壞掉\\壞掉.flac")));
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM files WHERE status = 'unreadable'")) == 2);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM tracks")) == audioFiles - 1);

    // 報告存進 scan_reports，下次啟動讀得回來
    LibraryWriter writer(*fx.db);
    const ScanReport saved = writer.lastReport();
    CHECK(saved.id == report.id);
    CHECK(saved.failed == 2);
    CHECK(saved.failures.size() == 2);
    CHECK(saved.finishedAt >= saved.startedAt);

    // 鎖解開後下一次掃描重讀，曲目補上
    const ScanReport retry = fx.scan();
    CHECK(retry.updated == 1);
    CHECK(retry.failed == 1);
}

TEST_CASE("AC-08-2：增量掃描只重讀新增、改過與上次讀不到的檔案") {
    Fixture fx(QStringLiteral("incremental"));
    const int audioFiles = buildSampleLibrary(fx.root);
    REQUIRE(fx.scan().added == audioFiles - 1);

    const ScanReport unchanged = fx.scan();
    CHECK(unchanged.scanned == audioFiles);
    CHECK(unchanged.added == 0);
    CHECK(unchanged.updated == 0);
    CHECK(unchanged.removed == 0);

    const qint64 trackId = fx.count(QStringLiteral(
        "SELECT t.id FROM tracks t JOIN files f ON f.id = t.file_id WHERE f.path LIKE '%01.flac'"));
    touchLater(fx.root + QStringLiteral("\\專輯 A\\01.flac"));
    QFile::remove(fx.root + QStringLiteral("\\DSD\\e.dff"));
    writeFlac(fx.root + QStringLiteral("\\新資料夾\\03.flac"), {});
    const ScanReport changed = fx.scan();
    CHECK(changed.added == 1);
    CHECK(changed.updated == 1);
    CHECK(changed.removed == 1);
    // 改過的檔案原地更新，曲目 id 不變
    CHECK(fx.count(QStringLiteral("SELECT t.id FROM tracks t JOIN files f ON f.id = t.file_id "
                                  "WHERE f.path LIKE '%01.flac'")) == trackId);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM folders WHERE name = '新資料夾'")) == 1);

    // 刪掉整個資料夾：資料夾與底下的曲目都移除；沒有曲目的專輯也移除
    QDir(fx.root + QStringLiteral("\\DSD")).removeRecursively();
    const ScanReport folderRemoved = fx.scan();
    CHECK(folderRemoved.removed == 1);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM folders WHERE name = 'DSD'")) == 0);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM albums WHERE title = 'DSD 測試'")) == 0);
    CHECK(fx.count(
              QStringLiteral("SELECT track_count FROM albums WHERE album_artist = 'MONACA'")) == 2);
}

TEST_CASE("AC-08-5：音樂資料夾暫時讀不到時記進報告，音樂庫不當成刪除") {
    Fixture fx(QStringLiteral("offline"));
    const int audioFiles = buildSampleLibrary(fx.root);
    fx.scan();
    const QString moved = fx.dir + QStringLiteral("\\離線");
    REQUIRE(QDir().rename(fx.root, moved));
    const ScanReport offline = fx.scan();
    REQUIRE(QDir().rename(moved, fx.root));
    CHECK(offline.removed == 0);
    CHECK(offline.failed == 1);
    REQUIRE(offline.failures.size() == 1);
    CHECK(offline.failures.first().path == fx.root);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM tracks")) == audioFiles - 1);
}

TEST_CASE("AC-08-1：掃描回報已掃描數／總數，可以取消且保留已寫入的部分") {
    Fixture fx(QStringLiteral("progress"));
    const int audioFiles = buildSampleLibrary(fx.root);
    std::vector<std::pair<int, int>> progress;
    ScanCallbacks callbacks;
    callbacks.progress = [&](int scanned, int total) {
        progress.emplace_back(scanned, total);
    };
    fx.scan(callbacks);
    REQUIRE(progress.size() >= 2);
    CHECK(progress.front() == std::pair{0, audioFiles});
    CHECK(progress.back() == std::pair{audioFiles, audioFiles});
    for (size_t i = 1; i < progress.size(); ++i) {
        CHECK(progress[i].first >= progress[i - 1].first);
    }

    // 列舉完之後取消：沒讀完，不移除任何東西
    QFile::remove(fx.root + QStringLiteral("\\DSD\\d.dsf"));
    touchLater(fx.root + QStringLiteral("\\專輯 A\\02.flac"));
    bool listed = false;
    ScanCallbacks cancelling;
    cancelling.progress = [&](int, int total) {
        listed = listed || total > 0;
    };
    cancelling.cancelled = [&] {
        return listed;
    };
    const ScanReport cancelled = fx.scan(cancelling);
    CHECK(cancelled.cancelled);
    CHECK(cancelled.removed == 0);
    CHECK(fx.count(QStringLiteral("SELECT count(*) FROM tracks")) == audioFiles - 1);
}
