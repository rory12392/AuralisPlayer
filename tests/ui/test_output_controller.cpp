// 輸出按鈕與輸出面板的資料（畫面規格 S-02、S-14）：輸出狀態的文字（AC-02-3）、裝置錯誤與
// 開不了的曲目的通知（AC-02-4、AC-01-3）、裝置清單與切換（AC-02-6）。用假的 PlaybackService
// 與假的輸出清單，不碰實體裝置。
#include "fake_outputs.h"
#include "fake_playback_service.h"
#include "shell/output_controller.h"
#include "ui/dialogs_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

using auralis::AudioFormat;
using auralis::OutputStatus;
using auralis::core::NotifierImpl;
using auralis::test::FakeOutputs;
using auralis::test::FakePlaybackService;
using auralis::ui::OutputController;

namespace {

QVariantMap row(const OutputController& output, int index) {
    return output.devices().at(index).toMap();
}

} // namespace

TEST_CASE("AC-02-3：位元完美時寫路徑、裝置、取樣率／位元深度與「位元完美」") {
    FakePlaybackService service;
    FakeOutputs outputs;
    outputs.current = outputs.options.at(0);
    OutputController output(service, nullptr, outputs.hooks());
    output.waitForDevices();

    CHECK_FALSE(output.active());
    CHECK(output.statusText() == QStringLiteral("未在播放"));
    CHECK(output.statusVariant() == QStringLiteral("idle"));
    CHECK(output.pathName() == QStringLiteral("WASAPI 獨占"));
    CHECK(output.deviceName() == QStringLiteral("Spring 3"));

    service.setOutput(QStringLiteral("WASAPI 獨占"), {}, {});
    CHECK(output.active());
    CHECK(output.formatText() == QStringLiteral("96 kHz / 24 bit"));
    CHECK(output.processingText() == QStringLiteral("無"));
    CHECK(output.statusText() == QStringLiteral("位元完美"));
    CHECK(output.statusVariant() == QStringLiteral("amber"));
    CHECK(output.accessibleText() ==
          QStringLiteral("輸出：WASAPI 獨占 Spring 3，96 kHz / 24 bit，位元完美"));
    // 選取中而且位元完美的那列寫 amber 的「位元完美」，其他列寫能力
    CHECK(row(output, 0).value(QStringLiteral("selected")).toBool());
    CHECK(row(output, 0).value(QStringLiteral("noteTone")).toString() == QStringLiteral("amber"));
    CHECK(row(output, 2).value(QStringLiteral("note")).toString() ==
          QStringLiteral("經 Windows 混音，非位元完美"));
}

TEST_CASE("AC-02-3：不是位元完美時說明是哪個處理改變了訊號，不當成錯誤") {
    FakePlaybackService service;
    FakeOutputs outputs;
    OutputController output(service, nullptr, outputs.hooks());
    service.setOutput(QStringLiteral("WASAPI 共享"),
                      {QStringLiteral("共享模式"), QStringLiteral("ReplayGain 已套用")}, {});
    CHECK_FALSE(output.bitPerfect());
    CHECK(output.statusText() == QStringLiteral("共享模式、ReplayGain 已套用"));
    CHECK(output.statusVariant() == QStringLiteral("neutral"));
    CHECK(output.processingText() == QStringLiteral("共享模式、ReplayGain 已套用"));
}

TEST_CASE("AC-04-2（T-49）：DSD 曲目的輸出按鈕寫「DSD64（DoP）／（原生）／（轉 PCM）」") {
    FakePlaybackService service;
    FakeOutputs outputs;
    OutputController output(service, nullptr, outputs.hooks());

    OutputStatus status;
    status.backend = QStringLiteral("WASAPI 獨占");
    status.bitPerfect = true;
    status.sourceFormat = AudioFormat{0, 1, 32, 2, false, 2822400};
    // DoP：裝置格式是 PCM（DSD 率 ÷ 16）
    status.deviceFormat = AudioFormat{176400, 24, 32, 2, false, 0};
    service.setOutputStatus(status);
    CHECK(output.formatText() == QStringLiteral("DSD64（DoP）"));

    // 原生 DSD：裝置格式就是 DSD
    status.backend = QStringLiteral("ASIO");
    status.deviceFormat = AudioFormat{0, 1, 32, 2, false, 2822400};
    service.setOutputStatus(status);
    CHECK(output.formatText() == QStringLiteral("DSD64（原生）"));

    // 轉 PCM：包裝器把「DSD 轉 PCM」放進原因；倍率照來源（DSD128）
    status.backend = QStringLiteral("WASAPI 獨占");
    status.sourceFormat = AudioFormat{0, 1, 32, 2, false, 5644800};
    status.deviceFormat = AudioFormat{176400, 24, 32, 2, false, 0};
    status.bitPerfect = false;
    status.reasons = {QStringLiteral("DSD 轉 PCM")};
    service.setOutputStatus(status);
    CHECK(output.formatText() == QStringLiteral("DSD128（轉 PCM）"));
    CHECK(output.accessibleText().contains(QStringLiteral("DSD128（轉 PCM）")));

    // PCM 曲目不受影響
    status.sourceFormat = AudioFormat{96000, 24, 32, 2, false, 0};
    status.deviceFormat = AudioFormat{96000, 24, 32, 2, false, 0};
    status.reasons.clear();
    service.setOutputStatus(status);
    CHECK(output.formatText() == QStringLiteral("96 kHz / 24 bit"));
}

TEST_CASE("AC-02-4：裝置錯誤時狀態改成錯誤與原因，通知列帶「重試」從停下來的位置再播") {
    FakePlaybackService service;
    FakeOutputs outputs;
    NotifierImpl notifier;
    OutputController output(service, &notifier, outputs.hooks());
    output.waitForDevices();
    service.setOutput({}, {}, QStringLiteral("輸出裝置已中斷連線"));

    CHECK(output.statusText() == QStringLiteral("輸出裝置已中斷連線"));
    CHECK(output.statusVariant() == QStringLiteral("signal"));
    CHECK(row(output, 0).value(QStringLiteral("noteTone")).toString() == QStringLiteral("signal"));
    const QVariantMap notice = notifier.current();
    CHECK(notice.value(QStringLiteral("kind")).toString() == QStringLiteral("error"));
    CHECK(notice.value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("輸出裝置已中斷連線")));
    CHECK(notice.value(QStringLiteral("actionText")).toString() == QStringLiteral("重試"));
    notifier.triggerAction(notice.value(QStringLiteral("id")).toInt());
    CHECK(service.calls().contains(QStringLiteral("play")));
}

TEST_CASE("AC-01-3：開不了的曲目在通知列寫「無法播放：<檔名>（原因）」") {
    FakePlaybackService service;
    FakeOutputs outputs;
    NotifierImpl notifier;
    OutputController output(service, &notifier, outputs.hooks());
    service.skip(QStringLiteral("C:/音樂/壞掉的.flac"), QStringLiteral("檔案沒有可以解碼的音訊"));
    CHECK(notifier.current().value(QStringLiteral("text")).toString() ==
          QStringLiteral("無法播放：壞掉的.flac（檔案沒有可以解碼的音訊），已跳到下一首"));
}

TEST_CASE("AC-02-6：輸出面板列出所有輸出路徑的裝置，選了就換，被佔用的不換並寫原因") {
    FakePlaybackService service;
    FakeOutputs outputs;
    NotifierImpl notifier;
    OutputController output(service, &notifier, outputs.hooks());
    output.waitForDevices();
    REQUIRE(output.devices().size() == 3);
    // 還沒選過時第一列就是第一次播放會用的
    CHECK(row(output, 0).value(QStringLiteral("selected")).toBool());

    output.select(2);
    CHECK(outputs.selected == QStringList{QStringLiteral("sh1")});
    CHECK(row(output, 2).value(QStringLiteral("selected")).toBool());
    CHECK(output.pathName() == QStringLiteral("WASAPI 共享"));

    outputs.busy.insert(QStringLiteral("ex2"));
    output.select(1);
    CHECK(row(output, 2).value(QStringLiteral("selected")).toBool());
    CHECK(row(output, 1).value(QStringLiteral("note")).toString() ==
          QStringLiteral("裝置被其他程式使用中"));
    CHECK(row(output, 1).value(QStringLiteral("noteTone")).toString() == QStringLiteral("signal"));
    // 選不了不是播放停止，不跳通知
    CHECK(notifier.current().isEmpty());

    // 重新打開面板時重新列舉：拔掉的裝置消失，上次選不了的原因清掉
    outputs.options.removeAt(0);
    output.setPanelOpen(true);
    output.waitForDevices();
    CHECK(output.devices().size() == 2);
    CHECK(row(output, 0).value(QStringLiteral("note")).toString() == QStringLiteral("位元完美"));
}

TEST_CASE("AC-02-6：記住的輸出裝置不在時狀態列的說明，使用者選了輸出後才清掉") {
    FakePlaybackService service;
    FakeOutputs outputs;
    OutputController output(service, nullptr, outputs.hooks());
    output.waitForDevices();
    QSignalSpy changed(&output, &OutputController::noticeChanged);
    const QString text =
        QStringLiteral("記住的輸出裝置「Spring 3」（WASAPI 獨占）不在，已改用預設裝置。");
    output.setNotice(text);
    CHECK(output.notice() == text);
    CHECK(changed.size() == 1);

    // 選不了（被佔用）時說明還在
    outputs.busy.insert(QStringLiteral("ex2"));
    output.select(1);
    CHECK(output.notice() == text);
    output.select(2);
    CHECK(output.notice().isEmpty());
    CHECK(changed.size() == 2);
}
