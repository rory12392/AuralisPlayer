#pragma once

#include "smtc_session.h"

#include <auralis/playback_service.h>

namespace auralis::mod::smtc {

/// 播放狀態 → 系統媒體控制顯示的狀態。
SmtcStatus statusFor(PlaybackService::State state) noexcept;

/// 系統媒體控制的按鈕 → 播放控制（AC-18-1）。只在 UI 執行緒呼叫；Other 不做事。
void applyButton(SmtcButton button, PlaybackService& playback);

} // namespace auralis::mod::smtc
