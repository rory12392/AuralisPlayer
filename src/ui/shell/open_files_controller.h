#pragma once

#include "library/open_paths.h"

#include <auralis/notifier.h>
#include <auralis/playback_service.h>

#include <QList>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QQmlEngine;
class QJSEngine;

namespace auralis::core {
class UiRegistryImpl;
}

namespace auralis::ui {

/// 把使用者給的路徑展開成曲目（主程式接 core::expandOpenPaths）。在工作執行緒呼叫。
using OpenPathsExpander = std::function<core::OpenPathsResult(const QStringList&)>;
/// 使用者給的路徑是不是資料夾（預設查檔案系統、不跟隨連結）。在工作執行緒呼叫。
using FolderCheck = std::function<bool(const QString&)>;

/// 開始播放的入口（AC-11-5）：從檔案總管拖進主視窗、或命令列給的檔案與資料夾，
/// 展開後加到佇列最後，並從加入的第一首開始播放；不需要先加入音樂庫。
/// 開不了的項目照 AC-01-3 的句型在通知列說明。QML 以單例 OpenFiles 取用；
/// 主程式在建立 QML 引擎前 setInstance。
/// 執行緒：只在 UI 執行緒使用；展開在自己的工作執行緒做（資料夾可能很大或在網路上），
/// 一次一批、照收到的順序加入佇列。
/// 展開之前先分流（T-61）：副檔名有模組用 UiRegistry::addFileOpenHandler 登記的檔案交給模組；
/// 播放清單檔（.m3u／.m3u8／.pls／.fpl）沒有模組登記（PL 停用）時不加進佇列，在通知列說明。
/// 只有檔案才分流，名稱像清單的資料夾照資料夾展開；檔案或資料夾在展開的工作執行緒判斷。
/// 模組的處理函式丟例外時在通知列說「無法匯入」。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class OpenFilesController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(OpenFiles)
    QML_SINGLETON

public:
    /// notifier 為 nullptr 時不通知（測試用）；registry 為 nullptr 時視為沒有模組登記開檔處理。
    OpenFilesController(PlaybackService& service, Notifier* notifier, OpenPathsExpander expander,
                        const core::UiRegistryImpl* registry = nullptr, QObject* parent = nullptr);
    ~OpenFilesController() override;

    /// 拖進視窗的網址（檔案總管給的是 file:///…）；不是本機檔案的照「只開啟本機檔案」略過。
    Q_INVOKABLE void openUrls(const QList<QUrl>& urls);
    /// 命令列或另一個執行個體轉來的完整路徑。
    void openPaths(const QStringList& paths);

    /// 測試用：等展開做完並套用結果。
    void waitForOpens();
    /// 測試用：換掉資料夾的判斷（預設查檔案系統）。
    void setFolderCheck(FolderCheck check) { m_isFolder = std::move(check); }

    static void setInstance(OpenFilesController* instance);
    static OpenFilesController* create(QQmlEngine*, QJSEngine*);

signals:
    /// 一批路徑處理完：加入佇列的曲目數（0 表示沒有可播放的）。
    void opened(int added);

private:
    void apply(const core::OpenPathsResult& result, const QStringList& paths);
    /// 名稱（只看字串）對得上模組登記的副檔名或播放清單：是檔案時要分流。
    bool routedByName(const QString& path) const;
    /// 把檔案交給模組（PL 停用時的播放清單在通知列說明），回傳沒有模組處理、要展開的路徑。
    QStringList routeToModules(const QStringList& files);

    PlaybackService& m_service;
    Notifier* m_notifier;
    OpenPathsExpander m_expander;
    FolderCheck m_isFolder;
    const core::UiRegistryImpl* m_registry;
    // 最後宣告、最先解構：解構時先等展開結束，結果才不會送到已拆掉的物件
    QThreadPool m_pool;
};

} // namespace auralis::ui
