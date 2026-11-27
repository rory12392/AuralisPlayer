#pragma once

#include <auralis/dialogs.h>
#include <auralis/library_query.h>
#include <auralis/notifier.h>

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis::core {
class LibraryManager;
}

namespace auralis::ui {

/// 歡迎畫面（S-01）、設定頁的音樂資料夾清單（S-16）與掃描報告（S-17）的資料：QML 以單例
/// Library 取用。包住核心的 LibraryManager：加入資料夾（經 Dialogs 選資料夾）、移除（經 Dialogs
/// 確認）、重新掃描、掃描進度的文字（AC-08-1）、掃描報告與讀不到的檔案（AC-08-5）；
/// 加入失敗與資料庫錯誤送進通知列。每個音樂資料夾的曲目數在自己的工作執行緒向 LibraryQuery 查。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class LibraryController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON
    /// 音樂資料夾：[{path, trackCount}]；曲目數還沒查好時是 -1。
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool hasFolders READ hasFolders NOTIFY foldersChanged)
    /// 音樂庫的曲目總數；還沒查好時是 -1。
    Q_PROPERTY(int trackCount READ trackCount NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY progressChanged)
    /// 0 到 1；總數還不知道時是 0。
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    /// 「12,480 / 40,112」；總數還不知道（列舉中）時是空字串。
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    /// 這次從畫面加入、正在掃描或剛掃描完的資料夾；沒有時是空字串。
    Q_PROPERTY(QString addedPath READ addedPath NOTIFY addedPathChanged)
    /// 最近一次掃描的報告：hasReport、tracks、added、removed、failed、cancelled、
    /// durationValue、durationUnit、finishedText、failures（[{path, reason}]）。
    Q_PROPERTY(QVariantMap report READ report NOTIFY reportChanged)

public:
    /// dialogs、notifier 為 nullptr 時不開對話框、不送通知（測試用）。
    LibraryController(core::LibraryManager& manager, const LibraryQuery& query, Dialogs* dialogs,
                      Notifier* notifier, QObject* parent = nullptr);
    ~LibraryController() override;

    QVariantList folders() const;
    bool hasFolders() const;
    int trackCount() const { return m_trackCount; }
    bool scanning() const;
    int scannedCount() const;
    int totalCount() const;
    qreal progress() const;
    QString progressText() const;
    QString addedPath() const { return m_addedPath; }
    QVariantMap report() const;

    /// 開資料夾選擇對話框，選好後加入音樂庫並開始掃描。
    Q_INVOKABLE void chooseFolder();
    /// 加入音樂資料夾並開始掃描；失敗時（不存在、已加入、互相包含）送通知、回傳 false。
    Q_INVOKABLE bool addFolder(const QString& path);
    /// 先問「移除音樂資料夾 <路徑>」，確認後才從音樂庫移除（不動磁碟上的檔案）。
    Q_INVOKABLE void confirmRemove(const QString& path);
    /// 增量重新掃描全部音樂資料夾。
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void cancelScan();
    /// 把讀不到的檔案清單（路徑與原因，一行一個）放進剪貼簿。
    Q_INVOKABLE void copyFailures();

    /// 讀不到的檔案清單的文字（copyFailures 放進剪貼簿的內容）。
    QString failuresText() const;
    /// 掃描報告的原因（核心的錯誤訊息）→ 給使用者看的分類：沒有讀取權限、檔案損毀；
    /// 其他照原文，但不顯示錯誤碼。
    static QString failureReason(const QString& error);
    /// 「12,480 / 40,112」（千分位）；總數是 0（還在列出檔案）時是空字串。
    static QString formatProgress(int scanned, int total);

    /// 測試用：等曲目數查完並套用結果。
    void waitForCounts();

    static void setInstance(LibraryController* instance);
    static LibraryController* create(QQmlEngine*, QJSEngine*);

signals:
    void foldersChanged();
    void scanningChanged();
    void progressChanged();
    void addedPathChanged();
    void reportChanged();

private:
    void refreshCounts();
    void applyCounts(const QHash<QString, int>& counts, int total);
    void notify(const QString& text, bool error);

    core::LibraryManager& m_manager;
    const LibraryQuery& m_query;
    Dialogs* m_dialogs;
    Notifier* m_notifier;
    QString m_addedPath;
    /// 資料夾的 pathKey → 曲目數。
    QHash<QString, int> m_counts;
    bool m_countsLoaded = false;
    int m_trackCount = -1;
    // 最後宣告、最先解構：解構時先等查詢結束，結果才不會送到已拆掉的物件
    QThreadPool m_pool;
};

} // namespace auralis::ui
