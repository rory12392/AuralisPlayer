#include "smtc_mapping.h"

namespace auralis::mod::smtc {

SmtcStatus statusFor(PlaybackService::State state) noexcept {
    switch (state) {
    case PlaybackService::State::Playing:
        return SmtcStatus::Playing;
    case PlaybackService::State::Paused:
        return SmtcStatus::Paused;
    case PlaybackService::State::Stopped:
        break;
    }
    return SmtcStatus::Stopped;
}

void applyButton(SmtcButton button, PlaybackService& playback) {
    // 鍵盤的播放／暫停鍵由系統依目前顯示的狀態送 Play 或 Pause，所以兩者分開對應，不用切換
    switch (button) {
    case SmtcButton::Play:
        playback.play();
        break;
    case SmtcButton::Pause:
        playback.pause();
        break;
    case SmtcButton::Stop:
        playback.stop();
        break;
    case SmtcButton::Next:
        playback.next();
        break;
    case SmtcButton::Previous:
        playback.previous();
        break;
    case SmtcButton::Other:
        break;
    }
}

} // namespace auralis::mod::smtc
