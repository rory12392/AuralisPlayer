#include "shell/jobs_controller.h"

#include "shell/core_bindings.h"

#include <QJSEngine>
#include <QTimer>

namespace auralis::ui {

JobsController::JobsController(JobRunner& runner, QObject* parent)
    : QObject(parent), m_runner(runner) {
    connect(&runner, &JobRunner::submitted, this, [this](quint64 id, const QString& name) {
        if (name == kScanJobName) {
            return;
        }
        m_jobs.insert(id, Job{name});
        // 不到 kShowDelayMs 就結束的工作在這之前已經被 finished 拿掉，不會出現
        QTimer::singleShot(kShowDelayMs, this, [this, id] {
            if (auto it = m_jobs.find(id); it != m_jobs.end()) {
                it->shown = true;
                emit changed();
            }
        });
    });
    connect(&runner, &JobRunner::progress, this, [this](quint64 id, double fraction) {
        if (auto it = m_jobs.find(id); it != m_jobs.end()) {
            it->progress = fraction;
            if (it->shown) {
                emit changed();
            }
        }
    });
    connect(&runner, &JobRunner::finished, this, [this](quint64 id) {
        if (auto it = m_jobs.find(id); it != m_jobs.end()) {
            const bool shown = it->shown;
            m_jobs.erase(it);
            if (shown) {
                emit changed();
            }
        }
    });
}

QMap<quint64, JobsController::Job>::const_iterator JobsController::current() const {
    for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it) {
        if (it->shown) {
            return it;
        }
    }
    return m_jobs.cend();
}

bool JobsController::active() const {
    return current() != m_jobs.cend();
}

QString JobsController::name() const {
    const auto it = current();
    return it != m_jobs.cend() ? it->name : QString();
}

qreal JobsController::progress() const {
    const auto it = current();
    return it != m_jobs.cend() && it->progress >= 0 ? it->progress : 0;
}

bool JobsController::indeterminate() const {
    const auto it = current();
    return it == m_jobs.cend() || it->progress < 0;
}

int JobsController::moreCount() const {
    int count = 0;
    for (const auto& job : m_jobs) {
        count += job.shown ? 1 : 0;
    }
    return count > 0 ? count - 1 : 0;
}

void JobsController::cancel() {
    const auto it = current();
    if (it == m_jobs.cend()) {
        return;
    }
    const quint64 id = it.key();
    m_runner.cancel(id);
    m_jobs.remove(id);
    emit changed();
}

JobsController* JobsController::create(QQmlEngine*, QJSEngine*) {
    auto* jobs = detail::jobs();
    Q_ASSERT_X(jobs, "JobsController", "主程式要在建立 QML 引擎前呼叫 bindCoreServices");
    // 核心的 JobRunner 擁有它，QML 的垃圾回收不能刪
    QJSEngine::setObjectOwnership(jobs, QJSEngine::CppOwnership);
    return jobs;
}

} // namespace auralis::ui
