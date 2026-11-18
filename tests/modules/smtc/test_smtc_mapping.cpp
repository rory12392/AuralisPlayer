// 系統媒體控制：播放狀態的對應、按鈕對應到播放控制（AC-18-1）。
#include "smtc_mapping.h"

#include "fake_playback_service.h"

#include <catch2/catch_test_macros.hpp>

using auralis::PlaybackService;
using auralis::mod::smtc::applyButton;
using auralis::mod::smtc::SmtcButton;
using auralis::mod::smtc::SmtcStatus;
using auralis::mod::smtc::statusFor;
using auralis::test::FakePlaybackService;

TEST_CASE("AC-18-1：播放、暫停、停止對應到系統媒體控制的同名狀態") {
    CHECK(statusFor(PlaybackService::State::Playing) == SmtcStatus::Playing);
    CHECK(statusFor(PlaybackService::State::Paused) == SmtcStatus::Paused);
    CHECK(statusFor(PlaybackService::State::Stopped) == SmtcStatus::Stopped);
}

TEST_CASE("AC-18-1：媒體鍵的播放、暫停、停止、上一首、下一首呼叫對應的播放控制") {
    FakePlaybackService playback;
    playback.reset(3, 1);
    applyButton(SmtcButton::Play, playback);
    applyButton(SmtcButton::Pause, playback);
    applyButton(SmtcButton::Next, playback);
    applyButton(SmtcButton::Previous, playback);
    applyButton(SmtcButton::Stop, playback);
    CHECK(playback.calls() == QStringList{QStringLiteral("play"), QStringLiteral("pause"),
                                          QStringLiteral("next"), QStringLiteral("previous"),
                                          QStringLiteral("stop")});
}

TEST_CASE("AC-18-1：沒有啟用的按鈕不動播放") {
    FakePlaybackService playback;
    playback.reset(3, 1);
    applyButton(SmtcButton::Other, playback);
    CHECK(playback.calls().isEmpty());
}
