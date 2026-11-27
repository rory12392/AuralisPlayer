// 播放中換輸出路徑（T-38 接 T-49 的 PlaybackService::outputPathChanging）：控制器在核心開新輸出前
// 換好 DSD 包裝器，不暫停與重播（不重入播放服務）；依 T-36 規則選實際方式（共享不提供 DoP）。
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
#include <vector>

using auralis::DsdSink;
using auralis::OutputStatus;
using auralis::PlaybackService;
using auralis::mod::out::DsdMode;
using auralis::mod::out::DsdModeController;
using auralis::test::FakePlaybackService;

namespace {

void ensureApp() {
    auralis::test::ensureQtApp("auralis_out_tests");
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

/// 記下每次註冊的包裝器。
class RecordingOutputs final : public auralis::OutputRegistry {
public:
    void add(std::shared_ptr<auralis::IOutputBackendFactory>) override {}
    void setDsdSink(std::shared_ptr<DsdSink> sink) override { sinks.push_back(sink); }
    std::vector<std::shared_ptr<DsdSink>> sinks;
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

OutputStatus dsdPlaying(const QString& backend) {
    OutputStatus status;
    status.backend = backend;
    status.sourceFormat = {0, 1, 32, 2, false, 2822400};
    status.deviceFormat = {176400, 24, 32, 2, false, 0};
    return status;
}

/// DSD 播放中（mode 是選的方式、backend 是目前的輸出路徑）的控制器與假服務。
struct PlayingRig {
    PlayingRig(const QString& mode, const QString& backend) {
        ensureApp();
        settings.values[QStringLiteral("dsdMode")] = mode;
        settings.values[QStringLiteral("outputPath")] = backend;
        playback.reset(1, 0);
        playback.play();
        playback.setOutputStatus(dsdPlaying(backend));
        controller =
            std::make_unique<DsdModeController>(settings, outputs, playback, nullptr, makeSink);
        QCoreApplication::sendPostedEvents();
        playback.reset(1, 0);
        playback.play();
    }
    /// 播放服務被呼叫過暫停或播放（reset 後的 play 不算）。
    bool reentered() const {
        const QStringList calls = playback.calls();
        return calls.count(QStringLiteral("pause")) > 0 || calls.count(QStringLiteral("play")) > 1;
    }
    MemorySettings settings;
    FakePlaybackService playback;
    RecordingOutputs outputs;
    std::unique_ptr<DsdModeController> controller;
};

} // namespace

TEST_CASE("換輸出路徑：ASIO 原生 DSD 播放中換到 WASAPI 獨占，改用 DoP 繼續、不暫停重播") {
    PlayingRig rig(QStringLiteral("native"), QStringLiteral("ASIO"));
    REQUIRE(rig.outputs.sinks.back()->name() == QStringLiteral("原生 DSD"));
    const auto before = rig.outputs.sinks.size();

    emit rig.playback.outputPathChanging(QStringLiteral("WASAPI 獨占"));
    // 通知是同步的：發完時新的包裝器已經註冊（核心接著才開新輸出）
    REQUIRE(rig.outputs.sinks.size() == before + 1);
    CHECK(rig.outputs.sinks.back()->name() == QStringLiteral("DoP"));
    // DoP 不改變位元流，經 WASAPI 獨占可以位元完美送出：核心不會因路徑不相容而跳過這首
    CHECK(rig.outputs.sinks.back()->reason().isEmpty());
    CHECK(rig.controller->effective() == DsdMode::Dop);
    CHECK(rig.controller->chosenMode() == DsdMode::Native);
    CHECK(rig.settings.values.value(QStringLiteral("outputPath")).toString() ==
          QStringLiteral("WASAPI 獨占"));
    CHECK_FALSE(rig.reentered());

    // 新輸出開好後的輸出狀態：已經一致，備援路徑不再換包裝器、也不暫停重播
    rig.playback.setOutputStatus(dsdPlaying(QStringLiteral("WASAPI 獨占")));
    QCoreApplication::sendPostedEvents();
    CHECK(rig.outputs.sinks.size() == before + 1);
    CHECK_FALSE(rig.reentered());
}

TEST_CASE("換輸出路徑：DoP 播放中換到 WASAPI 共享，改用轉 PCM（共享不提供 DoP）並說明") {
    PlayingRig rig(QStringLiteral("dop"), QStringLiteral("WASAPI 獨占"));
    REQUIRE(rig.outputs.sinks.back()->name() == QStringLiteral("DoP"));

    emit rig.playback.outputPathChanging(QStringLiteral("WASAPI 共享"));
    const auto& sink = rig.outputs.sinks.back();
    // 轉 PCM 的包裝器原因不是空的：核心照常開共享輸出，送的是 PCM、不是 DoP 封包（不出噪音）
    CHECK(rig.controller->effective() == DsdMode::Pcm);
    CHECK_FALSE(sink->reason().isEmpty());
    CHECK(sink->name() != QStringLiteral("DoP"));
    CHECK(rig.controller->note() ==
          QStringLiteral("WASAPI 共享模式會改動 DoP 封包，目前用 轉 PCM。"));
    // 選項上 DoP 停用並寫原因
    CHECK_FALSE(rig.controller->options().at(0).toMap().value(QStringLiteral("enabled")).toBool());
    CHECK_FALSE(rig.reentered());

    // 換回 WASAPI 獨占：恢復使用者選的 DoP
    emit rig.playback.outputPathChanging(QStringLiteral("WASAPI 獨占"));
    CHECK(rig.outputs.sinks.back()->name() == QStringLiteral("DoP"));
    CHECK_FALSE(rig.reentered());
}
