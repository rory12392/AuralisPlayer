// 原生 DSD 包裝器（AC-04-4 的核心路徑）：位元流不包裝直接交給後端；DSD 與 PCM 相鄰時
// 換格式的靜音在原生路徑也是 DSD 靜音碼 0x69（AC-04-6，核心 T-47 補的是包裝器轉好的靜音）。
#include "dop_test_helpers.h"
#include "dsd_fixtures.h"
#include "native_dsd_sink.h"

#include "audio/processor_chain_impl.h"

#include <catch2/catch_test_macros.hpp>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::mod::out::NativeDsdSink;
using namespace auralis::test;

namespace {

/// 先播 first 再接 second（格式不同，引擎排空、送靜音、重開裝置），回傳 Capture 的每一段紀錄。
QList<CaptureBackend::Session> playPair(const AudioFormat& firstFormat, const QByteArray& first,
                                        const AudioFormat& secondFormat, const QByteArray& second) {
    TrackList tracks;
    tracks.add(std::make_unique<MemorySource>(secondFormat, second), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<NativeDsdSink>());
    engine.setNextTrackProvider(&tracks);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine
                .play(EngineTrack{std::make_unique<MemorySource>(firstFormat, first), 0, -1, 1},
                      capture)
                .hasValue());
    REQUIRE(playToEnd(engine));
    CHECK_FALSE(engine.decodeError().has_value());
    return capture.sessions();
}

/// DSD64 的 kDopSilenceMs 毫秒有幾個位元組（2 聲道，每聲道每 32 位元一個樣本框）。
constexpr qsizetype kSilenceBytes = 2822400LL / 32 * kDopSilenceMs / 1000 * 8;

} // namespace

TEST_CASE("AC-04-4：原生 DSD 包裝器把 DSD 格式與位元流原樣交給後端，不算改變訊號") {
    NativeDsdSink sink;
    CHECK(sink.name() == QStringLiteral("原生 DSD"));
    CHECK_FALSE(sink.open(DsdFormat{}).hasValue());
    const auto opened = sink.open(DsdFormat::from(dsdAudio(5644800)));
    REQUIRE(opened.hasValue());
    CHECK(opened.value() == dsdAudio(5644800));
    CHECK(sink.maxOutputFrames(100) == 100);
    const QByteArray in = randomBytes(100 * 8, 401);
    QByteArray out(in.size(), '\0');
    CHECK(sink.convert(reinterpret_cast<const std::byte*>(in.constData()), 100,
                       reinterpret_cast<std::byte*>(out.data())) == 100);
    CHECK(out == in);
    CHECK(sink.reason().isEmpty());
}

TEST_CASE("AC-04-6：PCM 換到原生 DSD 時，裝置先收到至少 50 ms 的 DSD 靜音碼（0x69）再接曲目") {
    const QByteArray b = randomBytes(8000 * 8, 412);
    const auto sessions =
        playPair(pcmFormat(44100, 16), randomBytes(4410 * 4, 411), dsdAudio(2822400), b);
    REQUIRE(sessions.size() == 2);
    CHECK(sessions[1].format == dsdAudio(2822400));
    const qsizetype at = sessions[1].data.indexOf(b);
    REQUIRE(at >= kSilenceBytes);
    CHECK(allDsdSilence(sessions[1].data.left(at)));
}

TEST_CASE("AC-04-6：原生 DSD 換到 PCM 前，裝置在關閉前送出至少 50 ms 的 DSD 靜音碼（0x69）") {
    const QByteArray a = randomBytes(8000 * 8, 421);
    const QByteArray b = randomBytes(4410 * 4, 422);
    const auto sessions = playPair(dsdAudio(2822400), a, pcmFormat(44100, 16), b);
    REQUIRE(sessions.size() == 2);
    const QByteArray& dsd = sessions[0].data;
    REQUIRE(dsd.startsWith(a));
    const QByteArray tail = dsd.mid(a.size());
    CHECK(tail.size() >= kSilenceBytes);
    CHECK(allDsdSilence(tail));
    // PCM 那段開頭是數位靜音，曲目完整
    const qsizetype at = sessions[1].data.indexOf(b);
    REQUIRE(at >= 0);
    CHECK(allZeroBytes(sessions[1].data.left(at)));
}
