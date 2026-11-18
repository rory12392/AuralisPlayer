// DoP 包裝器：封包格式（AC-04-3）、經播放引擎與 Capture 後端、同格式相接、音量與處理器旁通、
// 模組註冊。經 WASAPI 獨占後端與換格式的靜音在 test_dop_switch.cpp。
#include "dop_sink.h"
#include "dop_test_helpers.h"
#include "dsd_fixtures.h"
#include "dsd_test_helpers.h"

#include "audio/processor_chain_impl.h"
#include "modules/module_manager.h"

#include <auralis/audio_processor.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QSettings>

#include <vector>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::IProcessor;
using auralis::IProcessorFactory;
using auralis::core::CaptureBackend;
using auralis::core::CoreServices;
using auralis::core::EngineTrack;
using auralis::core::ModuleManager;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::mod::out::DopSink;
using namespace auralis::test;

namespace {

/// 乘上固定增益的處理器（一律作用中）：DSD 經過它就會被改動，用來確認 DoP 旁通處理器鏈。
class GainProcessor final : public IProcessor {
public:
    void prepare(const AudioFormat&) override {}
    bool isActive() const noexcept override { return true; }
    QString reason() const override { return QStringLiteral("測試增益已套用"); }
    void process(double* samples, qint64 frames, int channels) noexcept override {
        for (qint64 i = 0; i < frames * channels; ++i) {
            samples[i] *= 0.5;
        }
    }
};

class GainFactory final : public IProcessorFactory {
public:
    QString name() const override { return QStringLiteral("gain"); }
    std::unique_ptr<IProcessor> create() override { return std::make_unique<GainProcessor>(); }
};

} // namespace

TEST_CASE("AC-04-3：DoP 的輸出格式是 DSD 取樣率 ÷ 16、24 位元放在 32 位元容器的整數 PCM") {
    struct Row {
        quint32 dsdRate;
        quint16 channels;
        quint32 pcmRate;
    };
    const Row row =
        GENERATE(Row{2822400, 2, 176400}, Row{5644800, 2, 352800}, Row{11289600, 2, 705600},
                 Row{2822400, 1, 176400}, Row{5644800, 6, 352800});
    DopSink sink;
    CHECK(sink.name() == QStringLiteral("DoP"));
    CHECK(sink.reason().isEmpty());
    const auto opened = sink.open(DsdFormat{row.dsdRate, row.channels});
    REQUIRE(opened.hasValue());
    const AudioFormat format = opened.value();
    CHECK(format.rate == row.pcmRate);
    CHECK(format.bits == 24);
    CHECK(format.container == 32);
    CHECK(format.channels == row.channels);
    CHECK_FALSE(format.isFloat);
    CHECK(format.dsdRate == 0);
    CHECK(sink.maxOutputFrames(4096) == 8192);
}

TEST_CASE("AC-04-3：不是 DSD 的格式，DoP 包裝器回傳錯誤") {
    DopSink sink;
    CHECK_FALSE(sink.open(DsdFormat{}).hasValue());
    CHECK_FALSE(sink.open(DsdFormat{2822400, 0}).hasValue());
}

TEST_CASE(
    "AC-04-3：每個 DSD 樣本框變成兩個 DoP 樣本框，標記 0x05／0xFA 交替，16 個 DSD 位元依時間順序") {
    DopSink sink;
    REQUIRE(sink.open(DsdFormat{2822400, 2}).hasValue());

    SECTION("第一個樣本的位元組排列") {
        // 左聲道 AB CD EF 01、右聲道 23 45 67 89（時間順序）
        const unsigned char in[] = {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89};
        std::vector<std::byte> out(static_cast<std::size_t>(sink.maxOutputFrames(1) * 8));
        REQUIRE(sink.convert(reinterpret_cast<const std::byte*>(in), 1, out.data()) == 2);
        const QByteArray bytes(reinterpret_cast<const char*>(out.data()), 16);
        // 小端 32 位元字：0、後 8 位、前 8 位、標記
        CHECK(bytes == QByteArray::fromHex("00cdab05"
                                           "00452305"
                                           "0001effa"
                                           "008967fa"));
    }
    SECTION("分好幾次轉換時標記照樣接續，資料位元和輸入相同") {
        const qsizetype frames = 1000;
        const QByteArray input = randomBytes(frames * 8, 71);
        QByteArray output;
        qsizetype done = 0;
        for (const qsizetype step : {qsizetype{1}, qsizetype{7}, qsizetype{300}, frames - 308}) {
            std::vector<std::byte> out(static_cast<std::size_t>(sink.maxOutputFrames(step) * 8));
            const qint64 got = sink.convert(
                reinterpret_cast<const std::byte*>(input.constData() + done * 8), step, out.data());
            REQUIRE(got == step * 2);
            output.append(reinterpret_cast<const char*>(out.data()),
                          static_cast<qsizetype>(got * 8));
            done += step;
        }
        const DopParse parsed = parseDop(output, 2);
        INFO(parsed.problem.toStdString());
        REQUIRE(parsed.problem.isEmpty());
        CHECK(parsed.firstMarker == 0x05);
        CHECK(parsed.dsd == input);
    }
}

TEST_CASE("AC-04-3：DSF 經播放引擎送到 Capture 後端的 DoP 封包，標記交替、資料位元和檔案相同") {
    const QString dir = freshDsdDir(QStringLiteral("dop-engine"));
    const qsizetype frames = 8192;
    const QList<QByteArray> channels{randomBytes(frames * 4, 81), randomBytes(frames * 4, 82)};
    DsfSpec spec;
    spec.channelData = channels;
    const QString path = writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec));

    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    CaptureBackend capture(fastCapture());
    auto status = engine.play(openDsdOk(path), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().bitPerfect);
    CHECK(status.value().reasons.isEmpty());
    CHECK(status.value().outputFormat == pcmFormat(176400, 24));
    REQUIRE(playToEnd(engine));
    CHECK(engine.underruns() == 0);

    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].format == pcmFormat(176400, 24));
    const qsizetype dopBytes = frames * 2 * 8;
    REQUIRE(sessions[0].data.size() >= dopBytes);
    const DopParse parsed = parseDop(sessions[0].data.left(dopBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.firstMarker == 0x05);
    CHECK(parsed.dsd == interleaveFrames(channels, frames, true));
}

TEST_CASE("AC-04-3：同格式的 DSD 曲目相接，DoP 標記跨曲目照樣交替、中間沒有插入東西") {
    const AudioFormat format = dsdAudio(2822400);
    const QByteArray a = randomBytes(5000 * 8, 91);
    const QByteArray b = randomBytes(7000 * 8, 92);
    TrackList tracks;
    tracks.add(std::make_unique<MemorySource>(format, b), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    engine.setNextTrackProvider(&tracks);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(EngineTrack{std::make_unique<MemorySource>(format, a), 0, -1, 1}, capture)
                .hasValue());
    REQUIRE(playToEnd(engine));
    CHECK(engine.underruns() == 0);
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 1);
    const qsizetype dopBytes = (a.size() + b.size()) * 2;
    REQUIRE(sessions[0].data.size() >= dopBytes);
    const DopParse parsed = parseDop(sessions[0].data.left(dopBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.dsd == a + b);
}

TEST_CASE("AC-04-3：DoP 播放時音量與處理器一律旁通，送出的位元和沒有它們時相同") {
    const AudioFormat format = dsdAudio(2822400);
    const QByteArray input = randomBytes(20000 * 8, 101);
    ProcessorChainImpl chain;
    chain.add(std::make_shared<GainFactory>());
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    engine.setVolume(30);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, input), capture);
    REQUIRE(status.hasValue());
    // 播放中再改一次音量：PCM 會把緩衝收回重做，DoP 不能被動到
    engine.setVolume(70);
    CHECK_FALSE(status.value().volumeAvailable);
    const PlaybackEngine::Status during = engine.status();
    CHECK(during.bitPerfect);
    CHECK(during.reasons.isEmpty());
    REQUIRE(playToEnd(engine));

    const QByteArray captured = capture.captured();
    const qsizetype dopBytes = input.size() * 2;
    REQUIRE(captured.size() >= dopBytes);
    const DopParse parsed = parseDop(captured.left(dopBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.dsd == input);
}

TEST_CASE("AC-04-3：進階輸出模組啟動時註冊 DoP 包裝器；停用時不註冊") {
    const QString dir = freshDsdDir(QStringLiteral("dop-module"));
    const QString ini = dir + QStringLiteral("\\settings.ini");
    SECTION("啟用") {
        CoreServices services;
        ModuleManager manager(services, ini);
        manager.startAll();
        const auto sink = services.outputs.dsdSink();
        REQUIRE(sink);
        CHECK(sink->name() == QStringLiteral("DoP"));
        manager.stopAll();
    }
    SECTION("停用") {
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(QStringLiteral("modules/OUT/enabled"), false);
        }
        CoreServices services;
        ModuleManager manager(services, ini);
        manager.startAll();
        CHECK_FALSE(services.outputs.dsdSink());
    }
}
