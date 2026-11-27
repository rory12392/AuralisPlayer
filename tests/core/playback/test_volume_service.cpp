// PlaybackService 的音量介面（AC-02-7）與輸出狀態的「音量 n%」（AC-02-3）、記在 settings.ini
// 重新啟動後沿用、DSD 直通時不可用。輸出用 Capture 後端，不碰實體裝置。
#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "playback/playback_service_impl.h"
#include "settings/volume_settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::OutputStatus;
using auralis::PlaybackService;
using auralis::TrackRef;
using auralis::core::CaptureBackend;
using auralis::core::OutputRegistryImpl;
using auralis::core::PlaybackServiceImpl;
using auralis::core::ProcessorChainImpl;
using auralis::core::savedVolume;
using auralis::core::SourceRegistryImpl;
using auralis::core::VolumeSettings;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using State = PlaybackService::State;

using auralis::test::cd;
using auralis::test::MemoryFactory;

namespace {

AudioFormat dsd64() {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

/// 永遠作用的處理器，模擬 ReplayGain 已套用。
class FakeReplayGain final : public auralis::IProcessor {
public:
    void prepare(const AudioFormat&) override {}
    bool isActive() const noexcept override { return true; }
    QString reason() const override { return QStringLiteral("ReplayGain 已套用"); }
    void process(double* samples, qint64 frames, int channels) noexcept override {
        for (qint64 i = 0; i < frames * channels; ++i) {
            samples[i] *= 0.5;
        }
    }
};

class FakeReplayGainFactory final : public auralis::IProcessorFactory {
public:
    QString name() const override { return QStringLiteral("假 ReplayGain"); }
    std::unique_ptr<auralis::IProcessor> create() override {
        return std::make_unique<FakeReplayGain>();
    }
};

/// 假的 DSD 直通包裝器（DoP、原生 DSD 是進階輸出模組的事）：位元流原樣放進 32 位元容器。
class FakeDirectSink final : public auralis::DsdSink {
public:
    QString name() const override { return QStringLiteral("直通（測試）"); }
    auralis::Result<AudioFormat> open(const DsdFormat& format) override {
        m_frameBytes = format.channels * 4;
        AudioFormat out;
        out.rate = format.dsdRate / 32;
        out.bits = 32;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        std::memcpy(out, dsd, static_cast<std::size_t>(dsdFrames * m_frameBytes));
        return dsdFrames;
    }

private:
    int m_frameBytes = 0;
};

struct Fixture {
    Fixture() {
        if (QCoreApplication::instance() == nullptr) {
            static int argc = 1;
            static char name[] = "auralis_core_tests";
            static char* argv[] = {name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        service.setOutputBackend(std::make_unique<CaptureBackend>(fastCapture()));
    }

    TrackRef add(const QString& name, double seconds, AudioFormat format = cd()) {
        const QString path = name + QStringLiteral(".mem");
        const qint64 rate = format.rate > 0 ? format.rate : format.dsdRate / 32;
        QByteArray data;
        if (format.dsdRate > 0) {
            data =
                QByteArray(static_cast<qsizetype>(rate * seconds) * format.bytesPerFrame(), '\x69');
        } else {
            data = makeNoise(format, static_cast<int>(rate * seconds), 7);
        }
        sourceFactory->tracks[path] = {format, data};
        return TrackRef{path, 0};
    }

    std::shared_ptr<MemoryFactory> sourceFactory = std::make_shared<MemoryFactory>();
    SourceRegistryImpl sources{sourceFactory};
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors, 17};
};

} // namespace

TEST_CASE("AC-02-7：音量預設 100%，設定後發出 volumeChanged，超出範圍的值取到 0–100") {
    Fixture f;
    CHECK(f.service.volume() == 100);
    CHECK(f.service.volumeAvailable());
    CHECK(f.service.volumeUnavailableReason().isEmpty());
    QSignalSpy changed(&f.service, &PlaybackService::volumeChanged);
    f.service.setVolume(60);
    CHECK(f.service.volume() == 60);
    REQUIRE(changed.size() == 1);
    CHECK(changed.at(0).at(0).toInt() == 60);
    // 同一個值不重複通知
    f.service.setVolume(60);
    CHECK(changed.size() == 1);
    f.service.setVolume(250);
    CHECK(f.service.volume() == 100);
    f.service.setVolume(-3);
    CHECK(f.service.volume() == 0);
    CHECK(changed.size() == 3);
}

TEST_CASE(
    "AC-02-3：音量不是 100% 時輸出狀態是非位元完美並寫出「音量 n%」，調回 100% 恢復位元完美") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0);
    f.service.playFrom({a}, 0);
    f.service.waitForStart();
    REQUIRE(f.service.state() == State::Playing);
    CHECK(f.service.outputStatus().bitPerfect);

    QSignalSpy statusChanged(&f.service, &PlaybackService::outputStatusChanged);
    f.service.setVolume(60);
    // 不等輪詢，設定的當下就更新
    REQUIRE(statusChanged.size() == 1);
    OutputStatus status = f.service.outputStatus();
    CHECK_FALSE(status.bitPerfect);
    CHECK(status.reasons == QStringList{QStringLiteral("音量 60%")});
    // 沒有重開裝置：還在播同一首
    CHECK(f.service.state() == State::Playing);

    f.service.setVolume(85);
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("音量 85%")});

    // 暫停時裝置關著，音量那一項照樣跟著現在的設定
    f.service.pause();
    REQUIRE(f.service.state() == State::Paused);
    f.service.setVolume(100);
    CHECK(f.service.outputStatus().bitPerfect);
    CHECK(f.service.outputStatus().reasons.isEmpty());
    f.service.setVolume(40);
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("音量 40%")});
    f.service.play();
    f.service.waitForStart();
    REQUIRE(f.service.state() == State::Playing);
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("音量 40%")});

    f.service.setVolume(100);
    status = f.service.outputStatus();
    CHECK(status.bitPerfect);
    CHECK(status.reasons.isEmpty());
}

TEST_CASE("AC-02-3：音量和其他處理（ReplayGain）可以同時列出，音量排在最後") {
    Fixture f;
    f.processors.add(std::make_shared<FakeReplayGainFactory>());
    const TrackRef a = f.add("a", 2.0);
    f.service.setVolume(70);
    f.service.playFrom({a}, 0);
    f.service.waitForStart();
    REQUIRE(f.service.state() == State::Playing);
    const OutputStatus status = f.service.outputStatus();
    CHECK_FALSE(status.bitPerfect);
    CHECK(status.reasons ==
          QStringList{QStringLiteral("ReplayGain 已套用"), QStringLiteral("音量 70%")});
    f.service.setVolume(100);
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("ReplayGain 已套用")});
    CHECK_FALSE(f.service.outputStatus().bitPerfect);
}

TEST_CASE("AC-02-7：DSD 直通播放中音量不可用並說明原因，換回 PCM 或停止後恢復") {
    Fixture f;
    f.outputs.setDsdSink(std::make_shared<FakeDirectSink>());
    const TrackRef dsd = f.add("dsd", 2.0, dsd64());
    const TrackRef pcm = f.add("pcm", 2.0);
    f.service.setVolume(50);
    QSignalSpy available(&f.service, &PlaybackService::volumeAvailableChanged);

    f.service.playFrom({dsd, pcm}, 0);
    f.service.waitForStart();
    REQUIRE(f.service.state() == State::Playing);
    CHECK_FALSE(f.service.volumeAvailable());
    CHECK(f.service.volumeUnavailableReason() == QStringLiteral("DSD 直通時無法調整音量"));
    REQUIRE(available.size() == 1);
    CHECK_FALSE(available.at(0).at(0).toBool());
    // 音量沒有作用在 DSD 上，輸出狀態不寫「音量 50%」
    CHECK(f.service.outputStatus().bitPerfect);
    CHECK(f.service.outputStatus().reasons.isEmpty());
    // 不可用時設的值照樣記下
    f.service.setVolume(30);
    CHECK(f.service.volume() == 30);
    CHECK(f.service.outputStatus().reasons.isEmpty());

    // 暫停中仍然是那首 DSD
    f.service.pause();
    CHECK_FALSE(f.service.volumeAvailable());

    // 換到 PCM 曲目：恢復可用，記下的音量開始作用
    f.service.playAt(1);
    f.service.waitForStart();
    REQUIRE(f.service.state() == State::Playing);
    CHECK(f.service.volumeAvailable());
    CHECK(f.service.volumeUnavailableReason().isEmpty());
    CHECK(f.service.outputStatus().reasons == QStringList{QStringLiteral("音量 30%")});

    f.service.playAt(0);
    f.service.waitForStart();
    CHECK_FALSE(f.service.volumeAvailable());
    f.service.stop();
    CHECK(f.service.volumeAvailable());
}

TEST_CASE("AC-02-7：音量記在 settings.ini 的 core/volume，重新啟動後沿用") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    // 沒有記過：100%
    CHECK(savedVolume(settingsPath) == 100);
    {
        Fixture f;
        VolumeSettings settings(f.service, settingsPath);
        CHECK(f.service.volume() == 100);
        f.service.setVolume(60);
        f.service.setVolume(35);
        // 連續改變時還沒寫檔（停下來一小段時間才寫一次）
        CHECK(savedVolume(settingsPath) == 100);
        // 這一輪結束時（解構）把還沒寫的寫進去
    }
    CHECK(savedVolume(settingsPath) == 35);
    const QSettings stored(settingsPath, QSettings::IniFormat);
    CHECK(stored.value(QStringLiteral("core/volume")).toInt() == 35);
    {
        // 「重新啟動」：新的播放服務套用記住的值
        Fixture f;
        QSignalSpy changed(&f.service, &PlaybackService::volumeChanged);
        VolumeSettings settings(f.service, settingsPath);
        CHECK(f.service.volume() == 35);
        CHECK(changed.size() == 1);
        // 停下來之後自己寫檔，不用等程式結束
        f.service.setVolume(80);
        QTest::qWait(VolumeSettings::kSaveDelayMs + 200);
        CHECK(savedVolume(settingsPath) == 80);
    }
}

TEST_CASE("AC-02-7：settings.ini 裡不合法的音量當成 100%") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    for (const QString& bad :
         {QStringLiteral("abc"), QStringLiteral("101"), QStringLiteral("-1")}) {
        QSettings(settingsPath, QSettings::IniFormat).setValue(QStringLiteral("core/volume"), bad);
        CAPTURE(bad.toStdString());
        CHECK(savedVolume(settingsPath) == 100);
    }
    QSettings(settingsPath, QSettings::IniFormat).setValue(QStringLiteral("core/volume"), 0);
    CHECK(savedVolume(settingsPath) == 0);
}
