#pragma once

#include "playback/playback_service_impl.h"

#include <auralis/result.h>

#include <QString>

namespace auralis::core {

/// 使用者選的輸出（輸出路徑＋裝置）記在 settings.ini 的 core/ 段，重新啟動後沿用
/// （US-02「設定頁的輸出裝置選擇」）。只記使用者自己選的；第一次播放時自動選的預設不記。
/// 執行緒：都在 UI 執行緒呼叫（會操作 PlaybackServiceImpl）。

/// 記住的輸出；沒有記過時是空的。
PlaybackServiceImpl::OutputChoice savedOutput(const QString& settingsPath);

/// 換到 choice（播放中從目前位置繼續，AC-02-6），成功才記住；選不了（被佔用、不見了）時
/// 不記，保留原本記住的。
Result<bool> selectOutput(PlaybackServiceImpl& playback, const QString& settingsPath,
                          const PlaybackServiceImpl::OutputChoice& choice);

/// 啟動時套用記住的輸出（還沒開始播，只記下選擇、不開裝置）。記住的裝置不在時保持預設
/// （第一次播放時的預設裝置），記住的值不清掉（裝置接回後下次啟動照樣沿用），回傳給狀態列的
/// 說明；套用成功或沒有記過時回傳空字串。
QString restoreOutput(PlaybackServiceImpl& playback, const QString& settingsPath);

} // namespace auralis::core
