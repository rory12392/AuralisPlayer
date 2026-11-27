#pragma once

// 模組停止的隔離測試（T-97）共用：「核心服務已經解構」的替身與佔住 JobRunner 執行緒的工作。
// 不真的解構服務（用到時是未定義行為、不一定當掉，這裡沒有 ASan），改成 gone 旗標設成 true 之後
// 每次呼叫記一次；停止後這個數字必須是 0。
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "test_qt_app.h"

#include <auralis/cover_service.h>
#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/library_query.h>
#include <auralis/module_database.h>
#include <auralis/notifier.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace auralis::test {

/// 「服務已經解構」的旗標與解構後的呼叫次數；同一個測試的假服務共用一份。
struct GoneFlag {
    std::atomic<bool> gone{false};
    mutable std::atomic<int> usedAfterGone{0};
    void touch() const {
        if (gone.load()) {
            ++usedAfterGone;
        }
    }
};

/// 音樂庫的替身：每個查詢都記一次（gone 之後），回傳空的結果。
class StopLibrary final : public LibraryQuery {
public:
    explicit StopLibrary(const GoneFlag& flag) : m_flag(flag) {}
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        m_flag.touch();
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
        m_flag.touch();
        return QList<TrackInfo>();
    }
    Result<QList<AlbumInfo>> albums() const override {
        m_flag.touch();
        return QList<AlbumInfo>();
    }
    Result<QStringList> artists() const override {
        m_flag.touch();
        return QStringList();
    }
    Result<QList<FolderInfo>> folders(qint64) const override {
        m_flag.touch();
        return QList<FolderInfo>();
    }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override {
        m_flag.touch();
        return QString();
    }
    Result<QList<TrackInfo>> tracksForDragQuery(const QString&) const override {
        m_flag.touch();
        return QList<TrackInfo>();
    }
    Result<QString> audioFilePath(const TrackRef& ref) const override {
        m_flag.touch();
        return ref.path;
    }

private:
    const GoneFlag& m_flag;
};

/// 模組資料庫的替身：轉給測試暫存裡真的資料庫（升到最新版），每次呼叫記一次（gone 之後）。
class StopDatabase final : public ModuleDatabase {
public:
    StopDatabase(const GoneFlag& flag, const QString& moduleCode) : m_flag(flag) {
        m_inner = std::make_unique<core::ModuleDatabaseImpl>(moduleCode, migratedPath());
    }
    Result<qint64> execute(const QString& sql, const QVariantList& params = {}) override {
        m_flag.touch();
        return m_inner->execute(sql, params);
    }
    Result<QList<Row>> query(const QString& sql, const QVariantList& params = {}) override {
        m_flag.touch();
        return m_inner->query(sql, params);
    }
    Result<bool> transaction(const std::function<bool()>& work) override {
        m_flag.touch();
        return m_inner->transaction(work);
    }

private:
    /// 升級資料庫要幾秒（Debug），整個測試行程只升一次，各測試共用（每次停止後才會碰的都被擋下）。
    static QString migratedPath() {
        static QTemporaryDir dir(QString::fromUtf8(AURALIS_TEST_TMP) +
                                 QStringLiteral("/t-97-stop-XXXXXX"));
        static const bool migrated =
            dir.isValid() &&
            core::migrateDatabase(dir.filePath(QStringLiteral("library.db"))).hasValue();
        REQUIRE(migrated);
        return dir.filePath(QStringLiteral("library.db"));
    }
    const GoneFlag& m_flag;
    std::unique_ptr<core::ModuleDatabaseImpl> m_inner;
};

/// 封面服務的替身：每次呼叫記一次（gone 之後），沒有封面。
class StopCovers final : public CoverService {
public:
    explicit StopCovers(const GoneFlag& flag) : m_flag(flag) {}
    std::optional<quint32> dominantColor(const QString&) override {
        m_flag.touch();
        return std::nullopt;
    }
    Result<QString> exportThumbnail(const QString&, int) override {
        m_flag.touch();
        return Error{QStringLiteral("沒有封面"), 0, QStringLiteral("exportThumbnail")};
    }

private:
    const GoneFlag& m_flag;
};

/// 對話框的替身：只記下儲存對話框的回呼，由測試決定選哪個檔。
class StopDialogs final : public Dialogs {
public:
    void chooseFolder(const QString&, PathsCallback) override {}
    void chooseFiles(const QString&, const QStringList&, PathsCallback) override {}
    void chooseSaveFile(const QString&, const QString&, const QStringList&,
                        PathsCallback done) override {
        pendingSave = std::move(done);
    }
    void confirm(const ConfirmRequest&, std::function<void(bool)>) override {}
    PathsCallback pendingSave;
};

/// 通知的替身：不做事。
class StopNotifier final : public Notifier {
public:
    void notify(const QString&, Kind, const QString&, std::function<void()>) override {}
};

/// 佔住 JobRunner 唯一一條執行緒的工作：建構後模組送的工作只能排隊，release() 才放行。
/// 用來重現「排隊中的工作在模組停止、服務解構之後才開始」（JobRunner::cancel 不會讓它不執行）。
class Blocker {
public:
    explicit Blocker(JobRunner& jobs) {
        auto state = m_state;
        jobs.submit(QStringLiteral("測試：佔住執行緒"), [state](JobContext&) {
            std::unique_lock lock(state->mutex);
            state->started = true;
            state->changed.notify_all();
            state->changed.wait(lock, [&] { return state->released; });
        });
        std::unique_lock lock(m_state->mutex);
        REQUIRE(m_state->changed.wait_for(lock, std::chrono::seconds(10),
                                          [&] { return m_state->started; }));
    }
    ~Blocker() { release(); }
    Blocker(const Blocker&) = delete;
    Blocker& operator=(const Blocker&) = delete;
    void release() {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->released = true;
        }
        m_state->changed.notify_all();
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool started = false;
        bool released = false;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();
};

/// 模組的工作把結果排回主執行緒（QCoreApplication），所以要有一個（整個測試程式一個）。
inline void ensureStopApp() {
    auralis::test::ensureQtApp("auralis_boundary_tests");
}

} // namespace auralis::test
