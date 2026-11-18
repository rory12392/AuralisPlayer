// DoP 經核心的 WASAPI 獨占後端（假裝置，AC-04-3）、DSD256 超過裝置上限（模組規格「沒涵蓋的情況」
// 第 3 條）、DSD 與 PCM 相鄰時換格式的靜音（AC-04-6）。
#include "dop_sink.h"
#include "dop_test_helpers.h"
#include "dsd_fixtures.h"
#include "fake_wasapi.h"

#include "audio/processor_chain_impl.h"
#include "audio/wasapi_backend.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <functional>
#include <tuple>
#include <vector>

using auralis::AudioFormat;
using auralis::OutputErrorKind;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::WasapiBackend;
using auralis::core::WasapiMode;
using auralis::mod::out::DopSink;
using namespace auralis::test;

namespace {

/// Spring 3 的 WASAPI 獨占實測：16、24-in-32、32 位元整數，44.1–384 kHz（不收 24-in-24 與浮點）。
std::function<bool(const WAVEFORMATEXTENSIBLE&)> spring3() {
    std::vector<std::tuple<DWORD, WORD, WORD>> formats;
    for (DWORD rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u}) {
        formats.emplace_back(rate, WORD{16}, WORD{16});
        formats.emplace_back(rate, WORD{32}, WORD{24});
        formats.emplace_back(rate, WORD{32}, WORD{32});
    }
    return supportsOnly(std::move(formats));
}

/// 先播 first 再接 second（格式不同，引擎排空、送靜音、重開裝置），回傳 Capture 的每一段紀錄。
QList<CaptureBackend::Session> playPair(const AudioFormat& firstFormat, const QByteArray& first,
                                        const AudioFormat& secondFormat, const QByteArray& second) {
    TrackList tracks;
    tracks.add(std::make_unique<MemorySource>(secondFormat, second), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
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

} // namespace

TEST_CASE("AC-04-3：DoP 經 WASAPI 獨占後端（Spring 3 的格式表），裝置收到的封包符合 DoP") {
    struct Row {
        quint32 dsdRate;
        DWORD pcmRate;
    };
    const Row row = GENERATE(Row{2822400, 176400}, Row{5644800, 352800});
    auto device = std::make_shared<FakeDevice>();
    device->supports = spring3();
    WasapiBackend backend(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device),
                          device->id);
    const QByteArray input = randomBytes(6000 * 8, 111);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    auto status =
        engine.play(std::make_unique<MemorySource>(dsdAudio(row.dsdRate), input), backend);
    REQUIRE(status.hasValue());
    CHECK(status.value().bitPerfect);
    REQUIRE(playToEnd(engine));

    const std::lock_guard lock(device->mutex);
    REQUIRE_FALSE(device->initialized.empty());
    const WAVEFORMATEXTENSIBLE& wave = device->initialized.back();
    CHECK(wave.Format.nSamplesPerSec == row.pcmRate);
    CHECK(wave.Format.wBitsPerSample == 32);
    CHECK(wave.Samples.wValidBitsPerSample == 24);
    CHECK(device->modes.back() == AUDCLNT_SHAREMODE_EXCLUSIVE);
    const QByteArray written = device->writtenData();
    const qsizetype dopBytes = input.size() * 2;
    REQUIRE(written.size() >= dopBytes);
    const DopParse parsed = parseDop(written.left(dopBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.dsd == input);
}

TEST_CASE("AC-04-3：裝置只收 24-in-24 時，後端打包成 3 位元組，DoP 封包照樣完整") {
    auto device = std::make_shared<FakeDevice>();
    device->supports = supportsOnly({{176400, 24, 24}});
    WasapiBackend backend(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device),
                          device->id);
    const QByteArray input = randomBytes(6000 * 8, 121);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    REQUIRE(
        engine.play(std::make_unique<MemorySource>(dsdAudio(2822400), input), backend).hasValue());
    REQUIRE(playToEnd(engine));
    const QByteArray written = device->writtenData();
    const qsizetype dopBytes = input.size() * 2 / 4 * 3;
    REQUIRE(written.size() >= dopBytes);
    const DopParse parsed = parseDop(written.left(dopBytes), 2, 3);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.dsd == input);
}

TEST_CASE(
    "DSD256 經 DoP 要 705.6 kHz，超過裝置上限時回報格式不支援（跳過這首），不改送別的取樣率") {
    auto device = std::make_shared<FakeDevice>();
    device->supports = spring3();
    WasapiBackend backend(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device),
                          device->id);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DopSink>());
    auto status = engine.play(
        std::make_unique<MemorySource>(dsdAudio(11289600), randomBytes(4096 * 8, 131)), backend);
    REQUIRE_FALSE(status.hasValue());
    CHECK(backend.lastErrorKind() == OutputErrorKind::FormatNotSupported);
    INFO(status.error().message.toStdString());
    CHECK(status.error().message.contains(QStringLiteral("705600 Hz")));
    const std::lock_guard lock(device->mutex);
    CHECK(device->initialized.empty());
}

TEST_CASE("AC-04-6：PCM 換到 DSD 時，DoP 裝置先送至少 50 ms 的 DoP 靜音封包（0x69）再接曲目") {
    const QByteArray b = randomBytes(8000 * 8, 142);
    const auto sessions =
        playPair(pcmFormat(44100, 16), randomBytes(4410 * 4, 141), dsdAudio(2822400), b);
    REQUIRE(sessions.size() == 2);
    CHECK(sessions[1].format == pcmFormat(176400, 24));

    // DoP 段：開頭的靜音、曲目，全部是連續的 DoP 封包
    const qsizetype silenceBytes = 176400LL * kDopSilenceMs / 1000 * 8;
    const qsizetype dopBytes = silenceBytes + b.size() * 2;
    REQUIRE(sessions[1].data.size() >= dopBytes);
    const DopParse parsed = parseDop(sessions[1].data.left(dopBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.firstMarker == 0x05);
    const qsizetype silenceDsd = parsed.dsd.size() - b.size();
    CHECK(silenceDsd >= silenceBytes / 2);
    CHECK(allDsdSilence(parsed.dsd.left(silenceDsd)));
    CHECK(parsed.dsd.mid(silenceDsd) == b);
}

TEST_CASE("AC-04-6：DSD 換到 PCM 時，PCM 裝置開頭是數位靜音、曲目完整") {
    const AudioFormat formatB = pcmFormat(44100, 16);
    const QByteArray a = randomBytes(8000 * 8, 151);
    const QByteArray b = randomBytes(4410 * 4, 152);
    const auto sessions = playPair(dsdAudio(2822400), a, formatB, b);
    REQUIRE(sessions.size() == 2);
    const DopParse parsed = parseDop(sessions[0].data.left(a.size() * 2), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.dsd == a);
    const qsizetype at = sessions[1].data.indexOf(b);
    REQUIRE(at >= 0);
    CHECK(at >= formatB.rate * kDopSilenceMs / 1000 * formatB.bytesPerFrame());
    CHECK(allZeroBytes(sessions[1].data.left(at)));
}

// DoP 裝置在換格式前排空時，render 補的要是 DoP 靜音封包而不是 0，否則 DAC 會在曲目結尾
// 掉出 DoP 模式、把 0 當 PCM 播（核心修正後補的是 DSD 靜音碼經包裝器轉好的封包）。
TEST_CASE("AC-04-6：DSD 換到 PCM 前，DoP 裝置在關閉前送出至少 50 ms 的 DoP 靜音封包") {
    const QByteArray a = randomBytes(8000 * 8, 161);
    const auto sessions =
        playPair(dsdAudio(2822400), a, pcmFormat(44100, 16), randomBytes(4410 * 4, 162));
    REQUIRE(sessions.size() == 2);
    const QByteArray& dop = sessions[0].data;
    const qsizetype silenceBytes = 176400LL * kDopSilenceMs / 1000 * 8;
    // 曲目之後到裝置關閉的整段（取成對的 DoP 樣本框）都要是 DoP 靜音
    const qsizetype tailBytes = (dop.size() - a.size() * 2) / 16 * 16;
    REQUIRE(tailBytes >= silenceBytes);
    const DopParse parsed = parseDop(dop.left(a.size() * 2 + tailBytes), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(allDsdSilence(parsed.dsd.mid(a.size())));
}
