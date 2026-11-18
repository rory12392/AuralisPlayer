#pragma once

#include <auralis/job_runner.h>

#include <QMap>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis::ui {

/// 主視窗狀態列的背景工作（介面規範 §6「背景工作」）：QML 以單例 Jobs 取用。訂閱 JobRunner 的
/// submitted、progress、finished；執行超過 kShowDelayMs 還沒結束的工作才顯示（歌詞查詢多半很快，
/// 避免狀態列閃一下）。同時有好幾個時顯示最早排入的那個，其餘以 moreCount 表示。
/// 掃描（名稱 kScanJobName）在狀態列有自己的一塊，這裡不顯示。
/// 執行緒：只在 UI 執行緒使用；JobRunner 的訊號從工作執行緒以 queued connection 送進來。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class JobsController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Jobs)
    QML_SINGLETON
    /// 有顯示中的工作。
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// 顯示的那個工作（最早排入）的名稱；沒有時空字串。
    Q_PROPERTY(QString name READ name NOTIFY changed)
    /// 0 到 1；indeterminate 時沒有意義。
    Q_PROPERTY(qreal progress READ progress NOTIFY changed)
    /// 這個工作還沒回報過進度。
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY changed)
    /// 其他顯示中的工作數（狀態列的「＋n 個」）。
    Q_PROPERTY(int moreCount READ moreCount NOTIFY changed)

public:
    static constexpr int kShowDelayMs = 300;
    /// 音樂庫掃描的工作名稱（核心 LibraryManager 用的名稱）。
    static inline const QString kScanJobName = QStringLiteral("音樂庫");

    explicit JobsController(JobRunner& runner, QObject* parent = nullptr);

    bool active() const;
    QString name() const;
    qreal progress() const;
    bool indeterminate() const;
    int moreCount() const;

    /// 取消顯示中的那個工作（JobRunner::cancel），並馬上從狀態列拿掉，不等工作自己結束。
    Q_INVOKABLE void cancel();

    static JobsController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    struct Job {
        QString name;
        double progress = -1; // < 0：還沒回報進度
        bool shown = false;
    };

    /// 顯示中、編號最小的工作；沒有時 end()。
    QMap<quint64, Job>::const_iterator current() const;

    JobRunner& m_runner;
    QMap<quint64, Job> m_jobs; // 依編號排序＝依排入順序
};

} // namespace auralis::ui
