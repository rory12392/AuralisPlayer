// DSD 輸出方式（T-36）：選項與停用原因、依輸出路徑換包裝器、播放中切換、裝置不支援的提示。
#include "dop_sink.h"
#include "dsd_mode.h"
#include "dsd_mode_controller.h"
#include "fake_playback_service.h"
#include "native_dsd_sink.h"
#include "pcm_convert_sink.h"
#include "test_qt_app.h"

#include <auralis/audio_output.h>
#include <auralis/module_settings.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QVariantMap>

#include <memory>

using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::OutputStatus;
using auralis::PlaybackService;
using auralis::mod::out::DsdMode;
using auralis::mod::out::DsdModeController;
using auralis::test::FakePlaybackService;

namespace {

/// 佇列的事件（輸出狀態送來的輸出路徑、通知）要有 QCoreApplication 才送得出去。
void ensureApp() {
    auralis::test::ensureQtApp("auralis_out_tests");
}

void flush() {
    QCoreApplication::sendPostedEvents();
}

class MemorySettings final : public auralis::ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { values[key] = value; }
    bool contains(const QString& key) const override { return values.contains(key); }
    void remove(const QString& key) override { values.remove(key); }
    QVariantMap values;
};

/// 記下每次註冊的包裝器，以及註冊當下播放服務的狀態。
class RecordingOutputs final : public auralis::OutputRegistry {
public:
    explicit RecordingOutputs(PlaybackService& playback) : m_playback(playback) {}
    void add(std::shared_ptr<auralis::IOutputBackendFactory>) override {}
    void setDsdSink(std::shared_ptr<DsdSink> sink) override {
        sinks.push_back(sink);
        statesAtSet.push_back(m_playback.state());
    }
    QString lastName() const { return sinks.empty() ? QString() : sinks.back()->name(); }
    std::vector<std::shared_ptr<DsdSink>> sinks;
    std::vector<PlaybackService::State> statesAtSet;

private:
    PlaybackService& m_playback;
};

std::shared_ptr<DsdSink> makeSink(DsdMode mode, quint32 pcmRate) {
    switch (mode) {
    case DsdMode::Native:
        return std::make_shared<auralis::mod::out::NativeDsdSink>();
    case DsdMode::Pcm:
        return std::make_shared<auralis::mod::out::PcmConvertSink>(pcmRate);
    case DsdMode::Dop:
        break;
    }
    return std::make_shared<auralis::mod::out::DopSink>();
}

struct Rig {
    Rig() { ensureApp(); }
    std::unique_ptr<DsdModeController> make() {
        return std::make_unique<DsdModeController>(settings, outputs, playback, nullptr, makeSink);
    }
    MemorySettings settings;
    FakePlaybackService playback;
    RecordingOutputs outputs{playback};
};

/// 正在播的狀態：backend 是輸出路徑，dsdRate 不是 0 時是 DSD 曲目（裝置格式 DoP 的 PCM）。
OutputStatus playingStatus(const QString& backend, quint32 dsdRate) {
    OutputStatus status;
    status.backend = backend;
    status.sourceFormat.dsdRate = dsdRate;
    status.sourceFormat.channels = 2;
    status.sourceFormat.rate = dsdRate == 0 ? 44100 : 0;
    status.deviceFormat = {dsdRate == 0 ? 44100u : dsdRate / 16, 24, 32, 2, false, 0};
    return status;
}

QVariantMap option(const DsdModeController& controller, int index) {
    return controller.options().at(index).toMap();
}

} // namespace

TEST_CASE("AC-04-2：預設 DSD 輸出方式是 DoP，啟動時註冊 DoP 包裝器") {
    Rig rig;
    auto controller = rig.make();
    CHECK(controller->modeIndex() == 0);
    REQUIRE(rig.outputs.sinks.size() == 1);
    CHECK(rig.outputs.lastName() == QStringLiteral("DoP"));
    CHECK(option(*controller, 2).value(QStringLiteral("text")).toString() ==
          QStringLiteral("轉 PCM 176.4 kHz"));
}

TEST_CASE("AC-04-2：ASIO 原生 DSD 只在輸出是 ASIO 時可選，其他輸出停用並寫原因") {
    Rig rig;
    auto controller = rig.make();
    // 沒記過輸出路徑：當成預設的 WASAPI 獨占
    CHECK_FALSE(option(*controller, 1).value(QStringLiteral("enabled")).toBool());
    CHECK(option(*controller, 1).value(QStringLiteral("reason")).toString() ==
          QStringLiteral("需選 ASIO"));
    controller->selectMode(1);
    CHECK(controller->chosenMode() == DsdMode::Dop);
    CHECK(rig.outputs.sinks.size() == 1);

    controller->setOutputPath(QStringLiteral("ASIO"));
    CHECK(option(*controller, 1).value(QStringLiteral("enabled")).toBool());
    controller->selectMode(1);
    CHECK(controller->modeIndex() == 1);
    CHECK(rig.outputs.lastName() == QStringLiteral("原生 DSD"));
    CHECK(rig.settings.values.value(QStringLiteral("dsdMode")).toString() ==
          QStringLiteral("native"));
    CHECK(rig.settings.values.value(QStringLiteral("outputPath")).toString() ==
          QStringLiteral("ASIO"));
}

TEST_CASE("AC-04-2：選了原生 DSD 後換到 WASAPI 獨占改用 DoP 並說明，換回 ASIO 恢復原生") {
    Rig rig;
    auto controller = rig.make();
    controller->setOutputPath(QStringLiteral("ASIO"));
    controller->selectMode(1);
    controller->setOutputPath(QStringLiteral("WASAPI 獨占"));
    CHECK(rig.outputs.lastName() == QStringLiteral("DoP"));
    CHECK(controller->modeIndex() == 0);
    CHECK(controller->chosenMode() == DsdMode::Native);
    CHECK(controller->note() == QStringLiteral("ASIO 原生 DSD 要選 ASIO 輸出才能用，目前用 DoP。"));
    controller->setOutputPath(QStringLiteral("ASIO"));
    CHECK(rig.outputs.lastName() == QStringLiteral("原生 DSD"));
    CHECK(controller->note().isEmpty());
}

TEST_CASE("AC-03-0：沒有 ASIO 外掛時原生 DSD 停用，原因「需要 ASIO 外掛」，記住的原生改用 DoP") {
    Rig rig;
    rig.settings.values.insert(QStringLiteral("dsdMode"), QStringLiteral("native"));
    rig.settings.values.insert(QStringLiteral("outputPath"), QStringLiteral("ASIO"));
    auto controller = std::make_unique<DsdModeController>(rig.settings, rig.outputs, rig.playback,
                                                          nullptr, makeSink, false);
    CHECK_FALSE(option(*controller, 1).value(QStringLiteral("enabled")).toBool());
    CHECK(option(*controller, 1).value(QStringLiteral("reason")).toString() ==
          QStringLiteral("需要 ASIO 外掛"));
    CHECK(controller->modeIndex() == 0);
    CHECK(rig.outputs.lastName() == QStringLiteral("DoP"));
    CHECK(controller->note() == QStringLiteral("原生 DSD 需要 ASIO 外掛，目前用 DoP。"));
    controller->selectMode(1);
    CHECK(controller->modeIndex() == 0);
    // 使用者選的方式照樣記住，裝回外掛後恢復
    CHECK(controller->chosenMode() == DsdMode::Native);
}

TEST_CASE("AC-04-2：WASAPI 共享不提供 DoP（會被混音器改動），改用轉 PCM") {
    Rig rig;
    auto controller = rig.make();
    controller->setOutputPath(QStringLiteral("WASAPI 共享"));
    CHECK_FALSE(option(*controller, 0).value(QStringLiteral("enabled")).toBool());
    CHECK(option(*controller, 0).value(QStringLiteral("reason")).toString() ==
          QStringLiteral("共享模式不支援"));
    CHECK(controller->modeIndex() == 2);
    CHECK(rig.outputs.lastName() == QStringLiteral("轉 PCM"));
    CHECK(controller->note() == QStringLiteral("WASAPI 共享模式會改動 DoP 封包，目前用 轉 PCM。"));
}

TEST_CASE("AC-04-5：轉 PCM 的取樣率可選 88.2／176.4／352.8 kHz，包裝器以選的取樣率輸出") {
    Rig rig;
    rig.settings.values[QStringLiteral("dsdPcmRate")] = 48000; // 不合法的值回預設
    auto controller = rig.make();
    CHECK(controller->pcmRate() == 176400);
    controller->selectMode(2);
    controller->selectPcmRate(2);
    CHECK(rig.settings.values.value(QStringLiteral("dsdPcmRate")).toUInt() == 352800);
    CHECK(rig.settings.values.value(QStringLiteral("dsdMode")).toString() == QStringLiteral("pcm"));
    CHECK(option(*controller, 2).value(QStringLiteral("text")).toString() ==
          QStringLiteral("轉 PCM 352.8 kHz"));
    const auto opened = rig.outputs.sinks.back()->open(DsdFormat{2822400, 2});
    REQUIRE(opened.hasValue());
    CHECK(opened.value().rate == 352800);
    // DoP 時改取樣率只記下，不換包裝器
    controller->selectMode(0);
    const auto count = rig.outputs.sinks.size();
    controller->selectPcmRate(0);
    CHECK(rig.outputs.sinks.size() == count);
}

TEST_CASE("情況 2：DSD 曲目播放中換方式，先暫停（記下位置）、換包裝器、再從原位置繼續") {
    Rig rig;
    rig.playback.reset(2, 0);
    auto controller = rig.make();
    rig.playback.play();
    rig.playback.setOutputStatus(playingStatus(QStringLiteral("WASAPI 獨占"), 2822400));
    flush();
    const auto before = rig.outputs.sinks.size();
    controller->selectMode(2);
    REQUIRE(rig.outputs.sinks.size() == before + 1);
    CHECK(rig.outputs.statesAtSet.back() == PlaybackService::State::Paused);
    CHECK(rig.playback.calls() ==
          QStringList{QStringLiteral("play"), QStringLiteral("pause"), QStringLiteral("play")});
    CHECK(rig.playback.state() == PlaybackService::State::Playing);
}

TEST_CASE("情況 2：PCM 曲目播放中或暫停中換方式，只換包裝器，不打斷播放") {
    Rig rig;
    rig.playback.reset(2, 0);
    auto controller = rig.make();
    rig.playback.play();
    rig.playback.setOutputStatus(playingStatus(QStringLiteral("WASAPI 獨占"), 0));
    flush();
    controller->selectMode(2);
    CHECK(rig.outputs.lastName() == QStringLiteral("轉 PCM"));
    CHECK(rig.playback.calls() == QStringList{QStringLiteral("play")});
    rig.playback.pause();
    rig.playback.setOutputStatus(playingStatus(QStringLiteral("WASAPI 獨占"), 2822400));
    controller->selectMode(0);
    CHECK(rig.outputs.lastName() == QStringLiteral("DoP"));
    CHECK(rig.playback.calls() == QStringList{QStringLiteral("play"), QStringLiteral("pause")});
}

TEST_CASE("輸出路徑從輸出狀態得知並記住，下次啟動在播放前就選對包裝器") {
    Rig rig;
    rig.settings.values[QStringLiteral("dsdMode")] = QStringLiteral("native");
    {
        auto controller = rig.make();
        CHECK(rig.outputs.lastName() == QStringLiteral("DoP"));
        rig.playback.setOutputStatus(playingStatus(QStringLiteral("ASIO"), 0));
        flush();
        CHECK(rig.outputs.lastName() == QStringLiteral("原生 DSD"));
    }
    auto again = rig.make();
    CHECK(rig.outputs.lastName() == QStringLiteral("原生 DSD"));
    CHECK(again->modeIndex() == 1);
}

TEST_CASE("情況 3：DSD 曲目因裝置不支援被跳過時，寫出原因並建議改用 ASIO 原生 DSD 或轉 PCM") {
    Rig rig;
    auto controller = rig.make();
    const QString reason =
        QStringLiteral("裝置不支援 705600 Hz 24-in-32 bit 2ch（獨占模式不重取樣）");
    // 不是 DSD 檔、或不是裝置不支援的原因：不提示
    rig.playback.skip(QStringLiteral("C:/t/a.flac"), reason);
    rig.playback.skip(QStringLiteral("C:/t/b.dsf"), QStringLiteral("檔案已損毀或格式不支援"));
    CHECK(controller->warning().isEmpty());
    rig.playback.skip(QStringLiteral("C:/t/DSD256.dsf"), reason);
    CHECK(controller->warning() == QStringLiteral("DSD256.dsf：") + reason +
                                       QStringLiteral("。請改用 ASIO 原生 DSD 或轉 PCM。"));
    // 換了方式就清掉
    controller->selectMode(2);
    CHECK(controller->warning().isEmpty());
    rig.playback.skip(QStringLiteral("C:/t/DSD256.DFF"), reason);
    CHECK(controller->warning().endsWith(QStringLiteral("請在設定頁改用較低的轉 PCM 取樣率。")));
}

TEST_CASE("AC-04-2：輸出狀態上的 DSD 輸出方式（DoP／原生／轉 PCM）") {
    using auralis::mod::out::dsdRouteLabel;
    CHECK(dsdRouteLabel(playingStatus(QStringLiteral("WASAPI 獨占"), 0)).isEmpty());
    CHECK(dsdRouteLabel(OutputStatus{}).isEmpty());
    CHECK(dsdRouteLabel(playingStatus(QStringLiteral("WASAPI 獨占"), 2822400)) ==
          QStringLiteral("DoP"));
    OutputStatus native = playingStatus(QStringLiteral("ASIO"), 2822400);
    native.deviceFormat = {0, 1, 32, 2, false, 2822400};
    CHECK(dsdRouteLabel(native) == QStringLiteral("原生"));
    OutputStatus pcm = playingStatus(QStringLiteral("ASIO"), 2822400);
    pcm.bitPerfect = false;
    pcm.reasons = {QStringLiteral("DSD 轉 PCM")};
    CHECK(dsdRouteLabel(pcm) == QStringLiteral("轉 PCM"));
}
