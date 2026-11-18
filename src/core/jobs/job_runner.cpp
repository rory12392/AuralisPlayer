#include <auralis/job_runner.h>

#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QThreadPool>

#include <exception>

Q_LOGGING_CATEGORY(lcJobs, "auralis.jobs")

namespace auralis {

struct JobContext::State {
    std::atomic<bool> cancelled{false};
};

JobContext::JobContext(std::shared_ptr<State> state, quint64 id, JobRunner* runner)
    : m_state(std::move(state)), m_id(id), m_runner(runner) {}

bool JobContext::isCancelled() const noexcept {
    return m_state->cancelled.load(std::memory_order_relaxed);
}

void JobContext::reportProgress(double fraction) {
    // 從工作執行緒發出；接收者在別的執行緒時 Qt 自動改成 queued connection
    emit m_runner->progress(m_id, fraction);
}

struct JobRunner::Impl {
    QThreadPool pool;
    QMutex mutex;
    QHash<quint64, std::shared_ptr<JobContext::State>> running;
    quint64 nextId = 1;
};

JobRunner::JobRunner(int maxThreads, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    if (maxThreads > 0) {
        m_impl->pool.setMaxThreadCount(maxThreads);
    }
}

JobRunner::~JobRunner() {
    cancelAll();
    m_impl->pool.waitForDone();
}

quint64 JobRunner::submit(const QString& name, Function function) {
    auto state = std::make_shared<JobContext::State>();
    quint64 id = 0;
    {
        QMutexLocker lock(&m_impl->mutex);
        id = m_impl->nextId++;
        m_impl->running.insert(id, state);
    }
    // 在排入執行緒池之前發出：工作再快結束，接收者也先收到 submitted 才收到 finished
    emit submitted(id, name);

    m_impl->pool.start([this, id, name, state, function = std::move(function)]() {
        JobContext context(state, id, this);
        Outcome outcome = Outcome::Succeeded;
        QString error;
        // 錯誤邊界：工作的例外不往外傳，只標記這個工作失敗
        try {
            function(context);
            if (context.isCancelled()) {
                outcome = Outcome::Cancelled;
            }
        } catch (const std::exception& e) {
            outcome = Outcome::Failed;
            error = QString::fromUtf8(e.what());
        } catch (...) {
            outcome = Outcome::Failed;
            error = QStringLiteral("未知的例外");
        }
        if (outcome == Outcome::Failed) {
            qCWarning(lcJobs).noquote() << "背景工作失敗：" << name << error;
        }
        {
            QMutexLocker lock(&m_impl->mutex);
            m_impl->running.remove(id);
        }
        emit finished(id, name, outcome, error);
    });
    return id;
}

void JobRunner::cancel(quint64 id) {
    QMutexLocker lock(&m_impl->mutex);
    if (auto it = m_impl->running.find(id); it != m_impl->running.end()) {
        it.value()->cancelled.store(true, std::memory_order_relaxed);
    }
}

void JobRunner::cancelAll() {
    QMutexLocker lock(&m_impl->mutex);
    for (const auto& state : std::as_const(m_impl->running)) {
        state->cancelled.store(true, std::memory_order_relaxed);
    }
}

bool JobRunner::waitForDone(int msecs) {
    return m_impl->pool.waitForDone(msecs);
}

} // namespace auralis
