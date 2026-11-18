#pragma once

// 音樂庫瀏覽模組測試的共用工具：Qt 事件迴圈、把測試資料夾掃成資料庫、轉接 LibraryQuery。
#include "library/library_query_impl.h"
#include "library/library_scanner.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"

#include <auralis/library_query.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <functional>

namespace auralis::test {

inline void ensureApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_lib_tests";
        static char* argv[] = {name, nullptr};
        new QCoreApplication(argc, argv); // 整個測試行程共用，不釋放
    }
}

/// 跑事件迴圈直到 condition 成立或逾時；回傳 condition 的最後結果。
inline bool waitFor(const std::function<bool()>& condition, int timeoutMs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return condition();
}

/// 建立 dbPath（升級到最新 schema）並把 musicRoot 掃進去。
inline void scanIntoDatabase(const QString& dbPath, const QString& musicRoot) {
    REQUIRE(core::migrateDatabase(dbPath).hasValue());
    auto opened = core::SqliteConnection::open(dbPath);
    REQUIRE(opened.hasValue());
    auto db = std::move(opened).value();
    core::LibraryWriter writer(*db);
    const QList<core::LibraryRoot> roots{{writer.ensureRoot(musicRoot), musicRoot}};
    core::scanLibrary(*db, roots, {});
}

/// 轉給真正的 LibraryQuery，另外記下 tracks() 被呼叫的次數，並可讓每次查詢多花 delayMs。
class CountingLibrary final : public LibraryQuery {
public:
    explicit CountingLibrary(const LibraryQuery& inner) : m_inner(inner) {}

    mutable std::atomic<int> calls{0};
    std::atomic<int> delayMs{0};

    Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                    quint32 subsong) const override {
        return m_inner.findByIdentity(path, subsong);
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        ++calls;
        if (delayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(delayMs.load()));
        }
        return m_inner.tracks(filter);
    }
    Result<QList<AlbumInfo>> albums() const override { return m_inner.albums(); }
    Result<QStringList> artists() const override { return m_inner.artists(); }
    Result<QList<FolderInfo>> folders(qint64 parentId) const override {
        return m_inner.folders(parentId);
    }
    Result<QString> readEmbeddedLyrics(const TrackRef& track) const override {
        return m_inner.readEmbeddedLyrics(track);
    }
    void emitScanFinished() { emit scanFinished(); }

private:
    const LibraryQuery& m_inner;
};

} // namespace auralis::test
