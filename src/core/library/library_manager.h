#pragma once

#include "library/library_writer.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <optional>

namespace auralis {
class JobRunner;
}

namespace auralis::core {

class LibraryQueryImpl;

/// 音樂資料夾與掃描的服務：歡迎畫面、設定頁的音樂資料夾清單、掃描進度與報告用；
/// 資料夾監看也經這裡。加入、移除、重新掃描都排進一個佇列、一次做一件，
/// 在 JobRunner 的工作裡寫資料庫，UI 執行緒不做 I/O。每件做完呼叫 LibraryQuery::scanFinished。
/// 執行緒：只在 UI 執行緒使用（需要事件迴圈：進度與結果以 queued 呼叫送回這個執行緒）。
class LibraryManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(int scannedCount READ scannedCount NOTIFY progressChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap lastReport READ lastReportMap NOTIFY lastReportChanged)

public:
    enum class AddResult { Added, AlreadyAdded, Overlaps, NotFound };
    Q_ENUM(AddResult)

    /// databasePath 是核心服務的欄位（第一次使用時才讀），要活得比這個物件久。
    /// 刻意晚綁定：CoreServices 先建構、主程式之後才設 databasePath，所以存參考；
    /// 呼叫端的字串要活得比物件久。
    LibraryManager(JobRunner& jobs, LibraryQueryImpl& query, const QString& databasePath,
                   QObject* parent = nullptr);
    /// 暫存字串會在建構後立刻消失（存下的參考懸空），所以在編譯期擋掉。
    LibraryManager(JobRunner& jobs, LibraryQueryImpl& query, QString&& databasePath,
                   QObject* parent = nullptr) = delete;
    /// 取消進行中的掃描並等它結束。
    ~LibraryManager() override;

    /// 目前的音樂資料夾（含已排進佇列、還沒寫進資料庫的）。
    QStringList folders();
    bool isScanning() const { return m_current.has_value(); }
    /// 進行中那件工作的已掃描數與總數；總數在列舉完之前是 0。
    int scannedCount() const { return m_scanned; }
    int totalCount() const { return m_total; }
    /// 最近一次完成的掃描；還沒掃描過時 id 為 0。
    ScanReport lastReport();
    /// 給 QML：id、startedAt、finishedAt、scanned、added、updated、removed、failed、cancelled、
    /// failures（[{path, error}]）。
    QVariantMap lastReportMap();

    /// 加入音樂資料夾並排入掃描。和既有的資料夾相同或互相包含時不加入。
    Q_INVOKABLE auralis::core::LibraryManager::AddResult addFolder(const QString& path);
    /// 從音樂庫移除（不動磁碟上的檔案）；正在掃描它時先取消。
    Q_INVOKABLE void removeFolder(const QString& path);
    /// 增量掃描：path 為空時掃全部音樂資料夾，否則只掃 path 所屬的那一個。
    Q_INVOKABLE void rescan(const QString& path = {});
    /// 取消進行中的掃描（已寫入的保留）與佇列中的掃描。
    Q_INVOKABLE void cancelScan();

signals:
    void foldersChanged();
    void scanningChanged();
    void progressChanged();
    void lastReportChanged();
    /// 一件工作因資料庫錯誤失敗（細節已寫日誌）。
    void operationFailed(const QString& error);

private:
    struct Op {
        enum class Kind { Add, Scan, Remove };
        Kind kind = Kind::Scan;
        QString path; ///< Scan 時空字串表示全部
    };
    struct JobState;

    void ensureLoaded();
    void startNext();
    void onJobFinished(quint64 id, bool failed, const QString& error);
    void setProgress(int scanned, int total);

    JobRunner& m_jobs;
    LibraryQueryImpl& m_query;
    const QString& m_databasePath;
    bool m_loaded = false;
    QStringList m_folders;
    std::optional<ScanReport> m_lastReport;
    QList<Op> m_pending;
    std::optional<Op> m_current;
    quint64 m_currentJob = 0;
    std::shared_ptr<JobState> m_state;
    int m_scanned = 0;
    int m_total = 0;
};

} // namespace auralis::core
