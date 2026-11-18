#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

class QThreadPool;

namespace auralis {

/// 交給背景工作的執行環境：查詢是否被取消、回報進度。只在工作自己的執行緒使用。
class JobContext {
public:
    /// 工作應該定期檢查，回傳 true 時盡快結束（可以直接 return）。
    bool isCancelled() const noexcept;
    /// 回報進度，fraction 介於 0 與 1；會以 JobRunner::progress 訊號送到 UI 執行緒。
    void reportProgress(double fraction);

private:
    friend class JobRunner;
    struct State;
    JobContext(std::shared_ptr<State> state, quint64 id, class JobRunner* runner);
    std::shared_ptr<State> m_state;
    quint64 m_id;
    class JobRunner* m_runner;
};

/// 背景工作（掃描、R128、縮圖、LRCLIB…）的進入點與錯誤邊界。
/// 工作丟出的例外在這裡接住，該工作標記為失敗，不影響其他工作與程式。
/// submit()、cancel() 只在 UI 執行緒呼叫；訊號在 JobRunner 所在的執行緒發出。
class JobRunner : public QObject {
    Q_OBJECT

public:
    enum class Outcome { Succeeded, Failed, Cancelled };
    Q_ENUM(Outcome)

    using Function = std::function<void(JobContext&)>;

    explicit JobRunner(int maxThreads = 0, QObject* parent = nullptr);
    /// 取消全部工作並等它們結束。
    ~JobRunner() override;

    /// 排入一個工作，回傳工作編號（從 1 開始）。
    quint64 submit(const QString& name, Function function);
    /// 要求取消；工作要自己檢查 JobContext::isCancelled()。
    void cancel(quint64 id);
    void cancelAll();
    /// 等全部工作結束；msecs < 0 表示一直等。回傳是否在時限內結束。
    bool waitForDone(int msecs = -1);

signals:
    /// submit() 排入工作時、在 submit() 返回前於呼叫端（UI）執行緒發出，早於這個工作的
    /// progress 與 finished。name 是 submit 的名稱（給使用者看的中文，主視窗狀態列會顯示）。
    /// 所有工作都可以 cancel(id)，所以沒有「可不可以取消」的參數。
    void submitted(quint64 id, const QString& name);
    void progress(quint64 id, double fraction);
    /// error 只在 Failed 時有內容（例外訊息，給日誌）。
    void finished(quint64 id, const QString& name, auralis::JobRunner::Outcome outcome,
                  const QString& error);

private:
    friend class JobContext;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace auralis
