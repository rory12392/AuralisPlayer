#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>

namespace auralis::core {
struct CoreServices;
class ModuleManager;
} // namespace auralis::core

namespace auralis::app {

class SingleInstance;
struct FullscreenSoakWatch;

/// 主視窗模式的命令列選項。
struct GuiOptions {
    /// --page：啟動後開這個頁面；沒有這個頁面時結束碼 2。
    QString page;
    /// --screenshot：截這個頁面後結束（搭配 --size、--theme、--out）。
    QString screenshotPage;
    QSize size;
    /// void 或 paper；空字串表示照設定。
    QString theme;
    QString outPath;
    /// 命令列給的檔案與資料夾（完整路徑）：加到佇列並開始播放（AC-11-5）。
    /// 截圖模式時輸出改用 Capture 後端（不開實體裝置），開好後才截圖。
    QStringList paths;
    /// 主要的執行個體：主視窗開好後接收另一個執行個體轉來的路徑。nullptr 表示不接收。
    SingleInstance* instance = nullptr;
    /// --self-check --bench-albumwall <資料夾>（開發建置）：把資料夾加進音樂庫、掃描完開
    /// library/albums 量封面牆捲動的幀率，結果寫進 benchResult 後結束。
    QString benchAlbumWall;
    QJsonObject* benchResult = nullptr;
    /// --self-check --soak <秒> --fullscreen：模組啟動後開主視窗的全螢幕頁，在另一條執行緒跑
    /// fullscreenSoak（UI 執行緒照常繪製），跑完結束事件迴圈。沒有全螢幕頁時不跑，
    /// 原因寫進 fullscreenError、結束碼 0（由自我檢查的 errors 回報）。
    /// 中途離開全螢幕或主視窗先結束時記在 fullscreenWatch（soak 要用它的 stop 盡快停）。
    std::function<void()> fullscreenSoak;
    QString* fullscreenError = nullptr;
    FullscreenSoakWatch* fullscreenWatch = nullptr;
};

/// 建立主視窗、啟動模組、開頁面，進入事件迴圈；回傳結束碼。
/// 主題與字型在 QML 引擎建立前決定；模組在主視窗建立後才啟動（PlatformWindow 才有 HWND）。
int runGui(const GuiOptions& options, core::CoreServices& services, core::ModuleManager& modules,
           const QString& settingsPath, const QElapsedTimer& sinceStart);

} // namespace auralis::app
