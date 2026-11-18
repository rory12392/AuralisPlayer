#include "library/library_manager.h"

#include "library/file_enumerator.h"
#include "library/library_query_impl.h"
#include "library/library_scanner.h"
#include "library/sqlite_connection.h"

#include <auralis/job_runner.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QSemaphore>
#include <QVariantList>

#include <algorithm>
#include <atomic>
#include <stdexcept>

Q_LOGGING_CATEGORY(lcLibrary, "auralis.library")

namespace auralis::core {

namespace {

/// 進度送回 UI 執行緒的最短間隔（4 萬首時避免每首一個事件）
constexpr qint64 kProgressIntervalMs = 100;

/// child 是否等於 parent 或在 parent 底下（都是 pathKey）。
bool coversKey(const QString& parent, const QString& child) {
    const QString prefix = parent.endsWith(QLatin1Char('\\')) ? parent : parent + QLatin1Char('\\');
    return child == parent || child.startsWith(prefix);
}

} // namespace

struct LibraryManager::JobState {
    std::atomic<bool> cancel{false};
    QSemaphore done;
    QMutex mutex;
    std::optional<ScanReport> report;
};

LibraryManager::LibraryManager(JobRunner& jobs, LibraryQueryImpl& query,
                               const QString& databasePath, QObject* parent)
    : QObject(parent), m_jobs(jobs), m_query(query), m_databasePath(databasePath) {
    connect(&m_jobs, &JobRunner::finished, this,
            [this](quint64 id, const QString&, JobRunner::Outcome outcome, const QString& error) {
                onJobFinished(id, outcome == JobRunner::Outcome::Failed, error);
            });
}

LibraryManager::~LibraryManager() {
    // 工作會呼叫這個物件，所以先讓它結束（取消後最多再讀完一批標籤）
    if (m_state) {
        m_state->cancel = true;
        m_state->done.acquire();
    }
}

void LibraryManager::ensureLoaded() {
    if (m_loaded || m_databasePath.isEmpty()) {
        return;
    }
    m_loaded = true;
    auto db = SqliteConnection::open(m_databasePath);
    if (!db) {
        qCWarning(lcLibrary).noquote() << "無法讀取音樂資料夾清單：" << db.error().message;
        return;
    }
    try {
        LibraryWriter reader(*db.value());
        for (const auto& root : reader.roots()) {
            m_folders.append(root.second);
        }
        const ScanReport report = reader.lastReport();
        if (report.id != 0) {
            m_lastReport = report;
        }
    } catch (const std::exception& e) {
        qCWarning(lcLibrary).noquote() << "無法讀取音樂資料夾清單：" << e.what();
    }
}

QStringList LibraryManager::folders() {
    ensureLoaded();
    return m_folders;
}

ScanReport LibraryManager::lastReport() {
    ensureLoaded();
    return m_lastReport.value_or(ScanReport{});
}

QVariantMap LibraryManager::lastReportMap() {
    const ScanReport report = lastReport();
    QVariantList failures;
    for (const auto& failure : report.failures) {
        failures.append(QVariantMap{{QStringLiteral("path"), failure.path},
                                    {QStringLiteral("error"), failure.error}});
    }
    return {{QStringLiteral("id"), report.id},
            {QStringLiteral("startedAt"), report.startedAt},
            {QStringLiteral("finishedAt"), report.finishedAt},
            {QStringLiteral("scanned"), report.scanned},
            {QStringLiteral("added"), report.added},
            {QStringLiteral("updated"), report.updated},
            {QStringLiteral("removed"), report.removed},
            {QStringLiteral("failed"), report.failed},
            {QStringLiteral("cancelled"), report.cancelled},
            {QStringLiteral("failures"), failures}};
}

LibraryManager::AddResult LibraryManager::addFolder(const QString& path) {
    ensureLoaded();
    const QString clean = cleanFolderPath(path);
    if (clean.isEmpty() || !QFileInfo(clean).isDir()) {
        return AddResult::NotFound;
    }
    const QString key = pathKey(clean);
    for (const auto& folder : std::as_const(m_folders)) {
        const QString existing = pathKey(folder);
        if (existing == key) {
            return AddResult::AlreadyAdded;
        }
        if (coversKey(existing, key) || coversKey(key, existing)) {
            return AddResult::Overlaps;
        }
    }
    m_folders.append(clean);
    emit foldersChanged();
    m_pending.append({Op::Kind::Add, clean});
    startNext();
    return AddResult::Added;
}

void LibraryManager::removeFolder(const QString& path) {
    ensureLoaded();
    const QString key = pathKey(cleanFolderPath(path));
    const auto found = std::find_if(m_folders.begin(), m_folders.end(),
                                    [&key](const QString& f) { return pathKey(f) == key; });
    if (found == m_folders.end()) {
        return;
    }
    const QString folder = *found;
    m_folders.erase(found);
    emit foldersChanged();

    m_pending.removeIf([&key](const Op& op) {
        return op.kind != Op::Kind::Remove && !op.path.isEmpty() && pathKey(op.path) == key;
    });
    bool rescanAll = false;
    if (m_current && m_current->kind != Op::Kind::Remove &&
        (m_current->path.isEmpty() || pathKey(m_current->path) == key)) {
        // 正在掃全部時，移除後把其他資料夾重新排一次（增量，沒變的不重讀）
        rescanAll = m_current->path.isEmpty();
        m_state->cancel = true;
    }
    m_pending.append({Op::Kind::Remove, folder});
    if (rescanAll) {
        m_pending.append({Op::Kind::Scan, {}});
    }
    startNext();
}

void LibraryManager::rescan(const QString& path) {
    const QString clean = path.isEmpty() ? QString() : cleanFolderPath(path);
    const bool queued = std::any_of(m_pending.cbegin(), m_pending.cend(), [&clean](const Op& op) {
        return op.kind == Op::Kind::Scan && op.path == clean;
    });
    if (!queued) {
        m_pending.append({Op::Kind::Scan, clean});
    }
    startNext();
}

void LibraryManager::cancelScan() {
    m_pending.removeIf([](const Op& op) { return op.kind == Op::Kind::Scan; });
    if (m_current && m_current->kind != Op::Kind::Remove) {
        m_state->cancel = true;
    }
}

void LibraryManager::startNext() {
    if (m_current || m_pending.isEmpty()) {
        return;
    }
    const Op op = m_pending.takeFirst();
    auto state = std::make_shared<JobState>();
    m_current = op;
    m_state = state;
    setProgress(0, 0);
    emit scanningChanged();

    const QString databasePath = m_databasePath;
    m_currentJob = m_jobs.submit(
        QStringLiteral("音樂庫"), [this, op, state, databasePath](JobContext& context) {
            struct Done {
                JobState& state;
                ~Done() { state.done.release(); }
            } done{*state};
            const auto cancelled = [&] {
                return state->cancel || context.isCancelled();
            };
            if (cancelled()) {
                return;
            }
            auto db = SqliteConnection::open(databasePath);
            if (!db) {
                throw std::runtime_error(db.error().message.toStdString());
            }
            LibraryWriter writer(*db.value());
            if (op.kind == Op::Kind::Remove) {
                writer.begin();
                try {
                    writer.deleteRoot(op.path);
                    writer.refreshAlbums();
                    writer.commit();
                } catch (...) {
                    writer.rollback();
                    throw;
                }
                return;
            }
            if (op.kind == Op::Kind::Add) {
                writer.ensureRoot(op.path);
            }
            QList<LibraryRoot> roots;
            for (const auto& [id, path] : writer.roots()) {
                if (op.path.isEmpty() || coversKey(pathKey(path), pathKey(op.path))) {
                    roots.append({id, path});
                }
            }
            QElapsedTimer sinceProgress;
            sinceProgress.start();
            ScanCallbacks callbacks;
            callbacks.cancelled = cancelled;
            callbacks.progress = [&](int scanned, int total) {
                if (sinceProgress.elapsed() < kProgressIntervalMs && scanned != total) {
                    return;
                }
                sinceProgress.restart();
                context.reportProgress(total == 0 ? 0.0 : double(scanned) / total);
                QMetaObject::invokeMethod(
                    this,
                    [this, state, scanned, total] {
                        if (m_state == state) {
                            setProgress(scanned, total);
                        }
                    },
                    Qt::QueuedConnection);
            };
            ScanReport report = scanLibrary(*db.value(), roots, callbacks);
            QMutexLocker lock(&state->mutex);
            state->report = std::move(report);
        });
}

void LibraryManager::onJobFinished(quint64 id, bool failed, const QString& error) {
    if (!m_current || id != m_currentJob) {
        return;
    }
    if (failed) {
        qCWarning(lcLibrary).noquote() << "音樂庫工作失敗：" << error;
        emit operationFailed(error);
    }
    {
        QMutexLocker lock(&m_state->mutex);
        if (m_state->report) {
            m_lastReport = *m_state->report;
            emit lastReportChanged();
        }
    }
    m_current.reset();
    m_state.reset();
    m_currentJob = 0;
    emit scanningChanged();
    m_query.notifyScanFinished();
    startNext();
}

void LibraryManager::setProgress(int scanned, int total) {
    m_scanned = scanned;
    m_total = total;
    emit progressChanged();
}

} // namespace auralis::core
