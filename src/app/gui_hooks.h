#pragma once

#include "shell/output_controller.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"

#include <auralis/library_query.h>

#include <QString>

#include <optional>

namespace auralis::core {
struct CoreServices;
class LibraryQueryImpl;
} // namespace auralis::core

namespace auralis::app {

// 主視窗殼的控制器向核心查資料、切輸出的接線（runGui 建立控制器時傳入）。

/// 佇列面板一列的曲目資訊從音樂庫查（工作執行緒）；查不到時 CUE 曲目讀 .cue，其他的畫面用檔名。
std::optional<ui::QueueTrackMeta> lookupTrack(const core::LibraryQueryImpl& library,
                                              const TrackRef& track);

/// 正在播放面板（S-11）的目前曲目資訊（工作執行緒）：專輯的曲目數再查一次音樂庫，
/// 位元率用音樂檔大小（只讀目錄資訊，不開檔）；CUE 曲目只是整軌檔的一段，位元率不算。
std::optional<ui::NowPlayingInfo> lookupNowPlaying(const core::LibraryQueryImpl& library,
                                                   const TrackRef& track);

/// 輸出面板（S-14）接核心：裝置清單來自 OutputRegistry（工作執行緒，只列舉不開裝置），
/// 切換與目前的選擇經 PlaybackService；選了的輸出記進 settings.ini，重新啟動後沿用。
ui::OutputHooks outputHooks(core::CoreServices& services, const QString& settingsPath);

} // namespace auralis::app
