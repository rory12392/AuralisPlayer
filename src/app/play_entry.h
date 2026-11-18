#pragma once

#include "shell/queue_model.h"

#include <auralis/library_query.h>

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class QQuickWindow;

namespace auralis::core {
struct CoreServices;
}
namespace auralis::ui {
class OpenFilesController;
}

namespace auralis::app {

class SingleInstance;

// 開始播放的入口（AC-11-5）在主視窗這一側的接線：命令列的路徑、另一個執行個體轉來的路徑。

/// 音樂庫裡沒有的 CUE 曲目（直接拖進來或命令列給的 .cue）：曲名、藝人、長度從 .cue 讀，
/// 佇列與播放列才不會每一軌都顯示 .cue 的檔名。不是 CUE 曲目或讀不到時 std::nullopt。
/// 執行緒：工作執行緒（讀 .cue）。
std::optional<ui::QueueTrackMeta> cueTrackMeta(const TrackRef& track);

/// 主要的執行個體：另一個執行個體轉來的路徑交給 openFiles，並把主視窗叫到前面。
void listenForPaths(SingleInstance& instance, ui::OpenFilesController& openFiles,
                    QQuickWindow* window);

/// 命令列的路徑加到佇列並開始播放。screenshot 為 true 時（截圖是測試模式）輸出改用 Capture 後端，
/// 不開實體音訊裝置、不發聲，並在標準輸出印一行結果（加入幾首、播放狀態、目前曲目）；
/// 開好、播一點之後呼叫 ready。
void openCommandLinePaths(core::CoreServices& services, ui::OpenFilesController& openFiles,
                          const QStringList& paths, bool screenshot, std::function<void()> ready);

/// 截圖（screenshotPage 不是空的）的 &panel=<面板 id 後半> 換成面板 id（queue → panel/queue）；
/// 不是截圖、沒有給或 closed（收合）時空字串。對不到已登記的面板（panels）時在標準錯誤印出原因
/// 並列出已登記的面板 id，回傳 nullopt。
std::optional<QString> screenshotPanelId(const QVariantList& panels, const QString& screenshotPage,
                                         const QVariantMap& params);

} // namespace auralis::app
