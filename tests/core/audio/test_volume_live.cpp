// 播放中改音量（AC-02-7）：不重開裝置、不掉樣本、增益連續、直通 ↔ 處理的切換點連續，以及改了之後
// 多久送進裝置。Capture 後端用實際時間的節奏，這三項在 tests/CMakeLists.txt 設成 RUN_SERIAL。
#include "audio/ffmpeg_source.h"
#include "engine_helpers.h"
#include "volume_probe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>

using auralis::AudioFormat;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::FfmpegSourceFactory;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::volumeGain;
using auralis::test::Analysis;
using auralis::test::analyze;
using auralis::test::encodeFile;
using auralis::test::hasPlateau;
using auralis::test::ListProvider;
using auralis::test::makeProbe;
using auralis::test::MemorySource;
using auralis::test::pcmFormat;
using auralis::test::realtimeCapture;
using auralis::test::sleepMs;
using auralis::test::stepLimit;
using auralis::test::waitFinished;

TEST_CASE("AC-02-7：播放中改音量不重開裝置、不掉樣本，增益連續過渡，回到 100% 後恢復逐位元相同") {
    const AudioFormat format = pcmFormat(48000, 24);
    const int frames = 48000 * 3;
    const QByteArray input = makeProbe(format, frames, 31);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend capture(realtimeCapture());
    REQUIRE(engine.play(std::make_unique<MemorySource>(format, input), capture).hasValue());

    sleepMs(300);
    engine.setVolume(60);
    CHECK_FALSE(engine.status().bitPerfect);
    CHECK(engine.status().reasons == QStringList{QStringLiteral("音量 60%")});
    sleepMs(300);
    engine.setVolume(20);
    sleepMs(300);
    engine.setVolume(100);
    CHECK(engine.status().bitPerfect);
    sleepMs(250);
    // 拖曳滑桿：連續的小變化
    for (int percent = 98; percent >= 30; percent -= 2) {
        engine.setVolume(percent);
        sleepMs(4);
    }
    sleepMs(250);
    engine.setVolume(100);
    REQUIRE(waitFinished(engine));

    CHECK(engine.underruns() == 0);
    // 沒有重開裝置：只有一次開啟的紀錄
    CHECK(capture.sessions().size() == 1);
    // 沒有多送或少送樣本
    CHECK(capture.sourceFrames() == frames);
    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    const Analysis analysis = analyze(input, output, format);
    // 右聲道的每個樣本都是「同一個位置的輸入 × 當時的增益」：收回重做沒有讓樣本錯位
    CHECK(analysis.worstAlignment <= 3.0);
    // 沒有不連續的跳變：相鄰樣本框的增益差不超過斜坡一步
    CHECK(analysis.maxStep <= stepLimit(format));
    // 中間確實到過各個音量
    CHECK(hasPlateau(analysis, volumeGain(60), format, 4800));
    CHECK(hasPlateau(analysis, volumeGain(20), format, 4800));
    CHECK(hasPlateau(analysis, volumeGain(30), format, 4800));
    // 改之前、最後回到 100% 之後都是整數直通：和輸入逐位元相同
    CHECK(analysis.firstChanged > 4800);
    CHECK(analysis.lastChanged < frames - 9600);
    // 直通與處理之間切換的那一個樣本框也連續（增益從 1.0 出發、回到 1.0）
    REQUIRE(analysis.firstChanged > 0);
    CHECK(std::abs(analysis.gain[static_cast<std::size_t>(analysis.firstChanged)] - 1.0) <=
          stepLimit(format));
    CHECK(std::abs(analysis.gain[static_cast<std::size_t>(analysis.lastChanged)] - 1.0) <=
          stepLimit(format));
}

TEST_CASE("AC-02-7：同格式接續的前後改音量，兩首之間不掉樣本、增益連續") {
    const AudioFormat format = pcmFormat(48000, 24);
    const QByteArray first = makeProbe(format, 48000, 41);
    const QByteArray second = makeProbe(format, 48000 * 2, 42);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    ListProvider provider;
    provider.tracks.push_back(
        EngineTrack{std::make_unique<MemorySource>(format, second), 0, -1, 2});
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(realtimeCapture());
    REQUIRE(engine.play(std::make_unique<MemorySource>(format, first), capture).hasValue());

    // 第一首播到一半時，第二首的開頭已經寫進緩衝：只能從第二首的開頭重做
    sleepMs(550);
    engine.setVolume(40);
    sleepMs(900);
    engine.setVolume(100);
    REQUIRE(waitFinished(engine));

    CHECK(engine.underruns() == 0);
    CHECK(capture.sessions().size() == 1);
    CHECK(capture.sourceFrames() == 48000 * 3);
    const QByteArray input = first + second;
    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    const Analysis analysis = analyze(input, output, format);
    CHECK(analysis.worstAlignment <= 3.0);
    CHECK(analysis.maxStep <= stepLimit(format));
    CHECK(hasPlateau(analysis, volumeGain(40), format, 4800));
    CHECK(analysis.lastChanged < 48000 * 3 - 9600);
}

TEST_CASE("AC-02-7：改音量到送進裝置的延遲在 0.2 秒內（FLAC 經 FFmpeg 音源，不重開裝置）") {
    const AudioFormat format = GENERATE(pcmFormat(44100, 16), pcmFormat(192000, 24));
    CAPTURE(format.rate, format.bits);
    const int frames = static_cast<int>(format.rate) * 2;
    const QByteArray input = makeProbe(format, frames, 51);
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-86-volume");
    QDir().mkpath(dir);
    const QString path =
        dir + QStringLiteral("/probe-%1-%2.flac").arg(format.rate).arg(format.bits);
    const QString why = encodeFile(path, "flac", "flac", format, input);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());

    FfmpegSourceFactory sources;
    auto source = sources.open(path);
    REQUIRE(source.hasValue());
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend capture(realtimeCapture());
    REQUIRE(engine.play(std::move(source).value(), capture).hasValue());

    sleepMs(600);
    // 這一刻已經送進裝置的樣本框數；第一個變小聲的樣本框和它的距離就是延遲
    const qint64 sentBefore = engine.renderedFrames();
    engine.setVolume(50);
    REQUIRE(waitFinished(engine));

    CHECK(engine.underruns() == 0);
    CHECK(capture.sourceFrames() == frames);
    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    const Analysis analysis = analyze(input, output, format);
    REQUIRE(analysis.firstChanged >= 0);
    const double latencyMs =
        static_cast<double>(analysis.firstChanged - sentBefore) * 1000.0 / format.rate;
    WARN("改音量到送進裝置：" << latencyMs << " ms（" << format.rate << " Hz / " << format.bits
                              << " bit）");
    CHECK(latencyMs > 0.0);
    CHECK(latencyMs < 200.0);
    CHECK(analysis.worstAlignment <= 3.0);
    CHECK(analysis.maxStep <= stepLimit(format));
    CHECK(hasPlateau(analysis, volumeGain(50), format, format.rate / 10));
}
