#pragma once

#include "r128.h"

#include <auralis/job_gate.h>
#include <auralis/job_runner.h>

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class LibraryQuery;
class ModuleDatabase;
class Notifier;
} // namespace auralis

namespace auralis::mod::out {

/// R128 計算（AC-06-3）：設定頁「計算整個音樂庫的 ReplayGain」與曲目右鍵選單「計算 ReplayGain」
/// 的入口。經 JobRunner 在背景量測，結果只寫 rg_computed（不寫音樂檔）；可取消，取消或程式關閉時
/// 已算完的保留、正在算的不寫（規格 §1 情況 5）。工作失敗只標記這個工作並在狀態說明（錯誤邊界）。
/// 新算的值從下一首（或重新開始這首）起作用。QML 以單例 R128 取用。
/// 執行緒：只在 UI 執行緒使用；量測在 JobRunner 的工作執行緒。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class R128Controller : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(R128)
    QML_SINGLETON
    Q_PROPERTY(bool running READ running NOTIFY changed)
    /// 0–1，running 時有意義。
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    /// 進行中、完成、取消、失敗的說明；還沒算過時空字串。
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    /// notifier 為 nullptr 時不送通知（測試用）。
    R128Controller(JobRunner& jobs, LibraryQuery& library, ModuleDatabase& database,
                   R128Opener open, Notifier* notifier, QObject* parent = nullptr);
    ~R128Controller() override;

    bool running() const { return m_job != 0; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    /// 整個音樂庫。已在算時不理會。
    Q_INVOKABLE void computeLibrary();
    /// 選取的曲目（TrackDragPayload 的 JSON，曲目右鍵選單給的）。已在算時不理會。
    Q_INVOKABLE void computeTracks(const QString& payloadJson);
    Q_INVOKABLE void cancel();
    /// 模組停止時：取消並等工作結束（最多 timeoutMs 毫秒，逾時寫日誌），之後才開始的工作
    /// 直接結束，不再碰音樂庫與資料庫。
    void shutdown(int timeoutMs = 7000) noexcept;

    static void setInstance(R128Controller* instance);
    static R128Controller* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    /// 工作和控制器之間的共用狀態：控制器先解構時工作照樣能寫結果。
    struct Shared {
        R128Summary summary;
    };

    void start(QList<TrackRef> tracks, bool wholeLibrary);
    void onProgress(quint64 id, double fraction);
    void onFinished(quint64 id, JobRunner::Outcome outcome, const QString& error);
    void setStatus(const QString& status);

    JobRunner& m_jobs;
    LibraryQuery& m_library;
    ModuleDatabase& m_database;
    R128Opener m_open;
    Notifier* m_notifier;
    quint64 m_job = 0;
    std::shared_ptr<Shared> m_shared;
    /// 計算工作與模組停止之間的閘門；工作也持有一份。
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
    double m_progress = 0.0;
    QString m_status;
};

/// 結果的說明（狀態與通知共用）。
QString r128SummaryText(const R128Summary& summary);

} // namespace auralis::mod::out
