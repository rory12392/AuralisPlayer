// 輸出狀態（AC-02-3）、裝置錯誤（AC-02-4）與切換輸出（AC-02-6）：PlaybackService 經
// OutputRegistry 的測試輸出路徑（Capture 加上可以注入的裝置錯誤，output_fixture.h）。
#include "output_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

TEST_CASE("AC-02-3：整數來源經不改變訊號的輸出路徑是位元完美，路徑與格式照實回報") {
    Fixture f;
    const TrackRef a = f.add("a", 2.0);
    f.service.playFrom({a}, 0);
    const OutputStatus status = f.service.outputStatus();
    CHECK(status.backend == QStringLiteral("測試輸出"));
    CHECK(status.sourceFormat == cd());
    CHECK(status.deviceFormat == cd());
    CHECK(status.bitPerfect);
    CHECK(status.reasons.isEmpty());
    // 第一次播放沒選過輸出時用第一個輸出路徑的第一個裝置，輸出面板據此標出選取的那列
    CHECK(f.service.outputChoice() ==
          Choice{QStringLiteral("測試路徑"), QStringLiteral("a"), QStringLiteral("a 裝置")});
}

TEST_CASE("AC-02-3：共享模式與浮點轉整數都不是位元完美，原因寫出是哪個處理") {
    Fixture f;
    f.device("a").signalChange = QStringLiteral("共享模式");
    f.device("b").integerOnly = true;
    // 夠長，切換兩次時都還在播
    const TrackRef a = f.add("a", 20.0, floatFormat());
    f.service.playFrom({a}, 0);
    CHECK_FALSE(f.service.outputStatus().bitPerfect);
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("共享模式")});

    REQUIRE(f.service.switchOutput(f.choice("b")));
    const OutputStatus status = f.service.outputStatus();
    CHECK_FALSE(status.bitPerfect);
    CHECK(status.reasons == QStringList{QStringLiteral("浮點轉成 32 bit 整數")});
    // 裝置收浮點時浮點來源原封不動，仍是位元完美
    f.device("b").integerOnly = false;
    REQUIRE(f.service.switchOutput(f.choice("b")));
    CHECK(f.service.outputStatus().bitPerfect);
}

TEST_CASE("AC-02-3：核心的 WASAPI 共享回報「共享模式」，輸出面板的能力說明照輸出路徑") {
    using auralis::core::WasapiMode;
    CHECK(auralis::core::WasapiBackend(WasapiMode::Shared, nullptr, {}).signalChange() ==
          QStringLiteral("共享模式"));
    CHECK(
        auralis::core::WasapiBackend(WasapiMode::Exclusive, nullptr, {}).signalChange().isEmpty());
    CHECK(auralis::core::WasapiBackendFactory(WasapiMode::Exclusive, nullptr).note() ==
          QStringLiteral("位元完美"));
    CHECK(auralis::core::WasapiBackendFactory(WasapiMode::Shared, nullptr).note() ==
          QStringLiteral("經 Windows 混音，非位元完美"));
}

TEST_CASE("AC-02-6：播放中切換輸出裝置，從目前位置繼續播") {
    Fixture f;
    const TrackRef a = f.add("a", 6.0);
    QSignalSpy tracks(&f.service, &PlaybackService::trackChanged);
    f.service.playFrom({a}, 0);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 1000; }));
    const qint64 at = f.service.positionMs();
    REQUIRE(f.service.switchOutput(f.choice("b")));
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.positionMs() == at);
    CHECK(f.service.outputChoice().deviceId == QStringLiteral("b"));
    REQUIRE(f.drive([&] { return f.service.state() == State::Stopped; }));
    // 新裝置收到的就是切換那一刻之後的資料
    const QByteArray& data = f.sourceFactory->tracks.at(a.path).second;
    const qint64 frame = at * 44100 / 1000;
    CHECK(startsWithThenSilence(f.factory->last->capture().captured(),
                                data.mid(frame * cd().bytesPerFrame())));
}

TEST_CASE("AC-02-6：選到被佔用的裝置時不切換，保留原本的輸出繼續播並回報原因") {
    Fixture f;
    const TrackRef a = f.add("a", 6.0);
    f.service.playFrom({a}, 0);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 500; }));
    f.device("b").busy = true;
    const auto switched = f.service.switchOutput(f.choice("b"));
    REQUIRE_FALSE(switched);
    CHECK(switched.error().message == QStringLiteral("裝置被其他程式使用中"));
    CHECK(f.service.outputChoice().deviceId == QStringLiteral("a"));
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.outputStatus().error.isEmpty());
}

TEST_CASE("AC-02-4：裝置被拔除時停止並顯示原因；裝置回來後再按播放從原位置繼續") {
    Fixture f;
    const TrackRef a = f.add("a", 6.0);
    QSignalSpy statuses(&f.service, &PlaybackService::outputStatusChanged);
    f.service.playFrom({a}, 0);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 1000; }));
    f.device("a").lost = true;
    REQUIRE(f.drive([&] { return f.service.state() == State::Stopped; }));
    const qint64 at = f.service.positionMs();
    CHECK(at > 1000);
    CHECK(f.service.outputStatus().error == QStringLiteral("輸出裝置已中斷連線"));
    REQUIRE_FALSE(statuses.isEmpty());
    CHECK(statuses.constLast()[0].value<OutputStatus>().error ==
          QStringLiteral("輸出裝置已中斷連線"));

    f.device("a").lost = false;
    f.service.play();
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.positionMs() == at);
    CHECK(f.service.outputStatus().error.isEmpty());
}

TEST_CASE("AC-02-4：開始播放時裝置被佔用，停止並顯示原因（不跳過曲目）") {
    Fixture f;
    const TrackRef a = f.add("a", 2.0);
    f.device("a").busy = true;
    QSignalSpy skipped(&f.service, &PlaybackService::trackSkipped);
    f.service.playFrom({a}, 0);
    CHECK(f.service.state() == State::Stopped);
    CHECK(f.service.outputStatus().error == QStringLiteral("裝置被其他程式使用中"));
    CHECK(skipped.isEmpty());
    // 釋放後再按播放就能播
    f.device("a").busy = false;
    f.service.play();
    CHECK(f.service.state() == State::Playing);
}
