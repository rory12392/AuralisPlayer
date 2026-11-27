#pragma once

// Qt Quick Test 的探針：狀態列的背景工作（tst_jobs）。
#include <auralis/job_runner.h>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

/// 狀態列的背景工作（tst_jobs，T-93）：在核心的 JobRunner 排入會一直執行、直到 finish(id) 或被取消
/// 才結束的工作。
class JobsProbe : public QObject {
    Q_OBJECT

public:
    /// 每個測試檔的引擎建立時換成那一份核心服務的 JobRunner。
    auralis::JobRunner* runner = nullptr;

    /// withProgress 時一開始就回報 0.4。回傳工作編號。
    Q_INVOKABLE int start(const QString& name, bool withProgress) {
        auto state = std::make_shared<State>();
        const auto id = runner->submit(name, [state, withProgress](auralis::JobContext& context) {
            if (withProgress) {
                context.reportProgress(0.4);
            }
            while (!state->release.load() && !context.isCancelled()) {
                QThread::msleep(5);
            }
            state->sawCancel.store(context.isCancelled());
        });
        const QMutexLocker lock(&m_mutex);
        m_states.insert(static_cast<int>(id), state);
        return static_cast<int>(id);
    }
    Q_INVOKABLE void finish(int id) {
        const QMutexLocker lock(&m_mutex);
        if (const auto state = m_states.value(id)) {
            state->release.store(true);
        }
    }
    Q_INVOKABLE void finishAll() {
        const QMutexLocker lock(&m_mutex);
        for (const auto& state : std::as_const(m_states)) {
            state->release.store(true);
        }
    }
    /// 工作結束時看到取消旗標。
    Q_INVOKABLE bool sawCancel(int id) const {
        const QMutexLocker lock(&m_mutex);
        const auto state = m_states.value(id);
        return state && state->sawCancel.load();
    }

private:
    struct State {
        std::atomic<bool> release = false;
        std::atomic<bool> sawCancel = false;
    };
    mutable QMutex m_mutex;
    QHash<int, std::shared_ptr<State>> m_states;
};
