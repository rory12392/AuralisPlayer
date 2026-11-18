#pragma once

#include "playlist_store.h"

#include <auralis/job_gate.h>

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class Dialogs;
class JobRunner;
class LibraryQuery;
class ModuleSettings;
class Notifier;
} // namespace auralis

namespace auralis::mod::pl {

/// 匯出 .m3u8（T-54，US-14；畫面規格 S-10），QML 以單例 PlaylistExport 取用。
/// - open(id)：播放清單頁的「匯出」與側欄項目右鍵「匯出」；設好 name、relative 後發 opened
///   （對話框在側欄區，收到就打開）。
/// - chooseAndExport(relative)：路徑模式記在 settings.ini 的 PL/exportPathMode，
///   開系統的儲存對話框（預設檔名「<名稱>.m3u8」）；
///   目標已存在時先確認「覆蓋 <檔名>」（AC-14-3），取消就不寫。
/// - 讀項目、查音樂庫、寫檔在 JobRunner 的工作裡（playlist_export.h），回到 UI 執行緒才通知：
///   「已匯出到 <路徑>」（有 CUE 曲目時加一句提醒），失敗時錯誤通知、原檔不動。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PlaylistExporter : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlaylistExport)
    QML_SINGLETON
    Q_PROPERTY(QString name READ name NOTIFY openedChanged)
    /// 上次選的路徑模式（對話框的「路徑」預設選哪一格）。
    Q_PROPERTY(bool relative READ relative NOTIFY openedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    /// jobs 沒給時在呼叫端的執行緒直接做（測試用）。
    /// settings 沒給時不記路徑模式；library 沒給時曲目全部當成音樂庫找不到。
    PlaylistExporter(PlaylistStore& store, Dialogs& dialogs, Notifier& notifier,
                     ModuleSettings* settings, const LibraryQuery* library, JobRunner* jobs,
                     QObject* parent = nullptr);

    QString name() const { return m_name; }
    bool relative() const { return m_relative; }
    bool busy() const { return m_busy; }

    /// 準備匯出這個播放清單（讀名稱與上次的路徑模式），發 opened；不存在時不做事。
    Q_INVOKABLE void open(qint64 id);
    /// 「選擇位置並匯出」：記下路徑模式，開儲存對話框，已存在先確認，再匯出。
    Q_INVOKABLE void chooseAndExport(bool relative);
    /// 模組停止：之後才開始的背景工作直接結束，等已經開始的結束（最多 timeoutMs，逾時寫日誌）。
    void shutdown(int timeoutMs) noexcept;

    static void setInstance(PlaylistExporter* instance);
    static PlaylistExporter* create(QQmlEngine*, QJSEngine*);

signals:
    void opened();
    void openedChanged();
    void busyChanged();
    /// 一次匯出結束（ok＝寫好了）；測試與畫面用。
    void finished(const QString& target, bool ok);

private:
    void confirmAndStart(const QString& target);
    void start(const QString& target);
    void setBusy(bool busy);

    PlaylistStore& m_store;
    Dialogs& m_dialogs;
    Notifier& m_notifier;
    ModuleSettings* m_settings = nullptr;
    const LibraryQuery* m_library = nullptr;
    JobRunner* m_jobs = nullptr;
    /// 匯出工作與模組停止之間的閘門（P-PL-24）；工作也持有一份。
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
    qint64 m_id = 0;
    QString m_name;
    bool m_relative = false;
    bool m_busy = false;
};

} // namespace auralis::mod::pl
