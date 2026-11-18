// DSD 轉 PCM 包裝器（AC-04-5）：格式與原因、已知頻率的測試音轉出後頻率與振幅正確、20 kHz 以上
// 被濾掉、靜音碼 0x69 收斂到接近 0、分段轉換與一次轉換相同、長時間轉換沒有不連續與溢位，
// 以及經核心播放引擎與 PCM 相鄰時換格式的靜音（AC-04-6）。
#include "dop_test_helpers.h"
#include "dsd_tone.h"
#include "pcm_convert_sink.h"

#include "audio/processor_chain_impl.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdlib>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::mod::out::PcmConvertSink;
using namespace auralis::test;

namespace {

constexpr double kOutRate = 176400.0;
/// 靜音輸出容許的最大值（24 位元的 LSB 數）。
constexpr std::int32_t kSilenceLsb = 2;

/// 一次轉完整段 DSD（2 聲道），回傳交錯的 24 位元整數樣本。
std::vector<std::int32_t> convertAll(PcmConvertSink& sink, const QByteArray& dsd) {
    const qint64 frames = dsd.size() / 8;
    QByteArray out(static_cast<qsizetype>(sink.maxOutputFrames(frames) * 8), '\0');
    const qint64 produced = sink.convert(reinterpret_cast<const std::byte*>(dsd.constData()),
                                         frames, reinterpret_cast<std::byte*>(out.data()));
    REQUIRE(produced <= sink.maxOutputFrames(frames));
    out.truncate(static_cast<qsizetype>(produced * 8));
    bool lowByteOk = false;
    auto samples = unpack24in32(out, &lowByteOk);
    CHECK(lowByteOk);
    return samples;
}

void openStereo(PcmConvertSink& sink, quint32 dsdRate) {
    REQUIRE(sink.open(DsdFormat::from(dsdAudio(dsdRate))).hasValue());
}

std::int32_t maxAbs(const std::vector<std::int32_t>& samples, std::size_t from = 0) {
    std::int32_t peak = 0;
    for (std::size_t i = from; i < samples.size(); ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    return peak;
}

} // namespace

TEST_CASE("AC-04-5：轉 PCM 包裝器以 176.4 kHz、24 位元（32 位元容器）輸出，原因是「DSD 轉 PCM」") {
    PcmConvertSink sink;
    CHECK(sink.name() == QStringLiteral("轉 PCM"));
    CHECK(sink.reason() == QStringLiteral("DSD 轉 PCM"));
    CHECK_FALSE(sink.open(DsdFormat{}).hasValue());
    for (const quint32 rate : {2822400u, 5644800u, 11289600u}) {
        const auto opened = sink.open(DsdFormat::from(dsdAudio(rate)));
        REQUIRE(opened.hasValue());
        CHECK(opened.value() == pcmFormat(176400, 24));
        // 每個 DSD 樣本框每聲道 32 個位元；DSD64 一框轉出 2 個樣本框、DSD256 兩框轉出 1 個
        CHECK(sink.maxOutputFrames(4096) == 4096LL * 32 * 176400 / rate);
    }
    CHECK(sink.maxOutputFrames(1) == 1); // DSD256 不足一個輸出樣本也要算進上限
    // 輸出取樣率由建構時給（設定 OUT/dsdPcmRate）；DSD 取樣率不是它的 8 的倍數倍時拒絕
    PcmConvertSink at88(88200);
    REQUIRE(at88.open(DsdFormat::from(dsdAudio(2822400, 1))).hasValue());
    CHECK(at88.open(DsdFormat::from(dsdAudio(2822400, 1))).value().rate == 88200);
    PcmConvertSink at48(48000);
    CHECK_FALSE(at48.open(DsdFormat::from(dsdAudio(2822400))).hasValue());
}

TEST_CASE("AC-04-5：DSD64／128／256 的 1 kHz 測試音轉出後頻率與振幅正確，20 kHz 以上的雜訊被濾掉") {
    const quint32 dsdRate = GENERATE(2822400u, 5644800u, 11289600u);
    CAPTURE(dsdRate);
    constexpr double kAmplitude = 0.25;
    PcmConvertSink sink;
    openStereo(sink, dsdRate);
    DsdTone tone(dsdRate, 1000.0, kAmplitude);
    const auto samples = convertAll(sink, stereoToneDsd(tone, dsdRate / 32 / 2)); // 0.5 秒
    REQUIRE(samples.size() == 176400u / 2 * 2);
    const auto left = channelOf(samples, 2, 0);
    const auto right = channelOf(samples, 2, 1);
    // 第 2 聲道是逐位元反相的同一個訊號：轉出來剛好是負的（聲道沒有排錯、沒有混到）。
    // 開頭濾波器長度內不比：兩個聲道的歷史一開始都是靜音碼 0x69，不是彼此的反相
    const auto settle = static_cast<std::size_t>(sink.taps()) * 176400 / dsdRate + 1;
    for (std::size_t i = settle; i < left.size(); ++i) {
        REQUIRE(right[i] == -left[i]);
    }
    // 跳過第 1 毫秒（靜音接測試音、調變器起步的過渡），量其餘 0.499 秒
    const std::vector<double> body(left.begin() + 176, left.end());
    const ToneFit fit = fitTone(body, kOutRate, 1000.0);
    CAPTURE(fit.amplitude, toDb(fit.residualRms));
    CHECK(std::abs(toDb(fit.amplitude / kAmplitude)) < 0.05);
    CHECK(risingZeroCrossings(body) >= 498);
    CHECK(risingZeroCrossings(body) <= 500);
    // 擬合後剩下的是雜訊：DSD 的雜訊集中在 20 kHz 以上，沒濾掉時約 −15 dBFS；濾掉後要低於 −60 dBFS
    CHECK(toDb(fit.residualRms) < -60.0);
}

TEST_CASE("AC-04-5：阻帶的 100 kHz 測試音轉出後低於 −100 dBFS（不會折疊回可聽範圍）") {
    const quint32 dsdRate = GENERATE(2822400u, 5644800u, 11289600u);
    CAPTURE(dsdRate);
    PcmConvertSink sink;
    openStereo(sink, dsdRate);
    DsdTone tone(dsdRate, 100000.0, 0.25);
    const auto left =
        channelOf(convertAll(sink, stereoToneDsd(tone, dsdRate / 32 / 4)), 2, 0); // 0.25 秒
    // 100 kHz 在 176.4 kHz 取樣下會折疊到 76.4 kHz
    const ToneFit fit = fitTone(left, kOutRate, kOutRate - 100000.0);
    CAPTURE(toDb(fit.amplitude));
    CHECK(toDb(fit.amplitude) < -100.0);
}

TEST_CASE("AC-04-5：DSD 靜音碼 0x69 轉出接近 0，有聲音之後接靜音也收斂到接近 0") {
    const quint32 dsdRate = GENERATE(2822400u, 5644800u, 11289600u);
    CAPTURE(dsdRate);
    PcmConvertSink sink;
    openStereo(sink, dsdRate);
    const qsizetype tenthFrames = dsdRate / 32 / 10;
    const QByteArray silence(tenthFrames * 8, kDsdSilence);
    // 一開始就是靜音（濾波歷史在 open 時填好靜音碼，沒有暖機暫態）
    CHECK(maxAbs(convertAll(sink, silence)) <= kSilenceLsb);
    constexpr double kAmplitude = 0.5;
    DsdTone tone(dsdRate, 1000.0, kAmplitude);
    const auto loud = convertAll(sink, stereoToneDsd(tone, tenthFrames));
    // 突然停在波形中間：濾波後的過衝（吉布斯現象）不超過振幅的 10%，不會出現大的突波
    const auto after = convertAll(sink, silence);
    CHECK(maxAbs(after) <= static_cast<std::int32_t>(kAmplitude * 1.1 * 8388608));
    // 濾波器長度的歷史換成靜音之後就收斂（輸出樣本數 = 位元數 ÷ 抽取倍數，2 聲道）
    const std::size_t settle = static_cast<std::size_t>(sink.taps()) * 176400 / dsdRate * 2 + 2;
    CHECK(maxAbs(after, settle) <= kSilenceLsb);
    CHECK(maxAbs(loud) > static_cast<std::int32_t>(kAmplitude * 0.9 * 8388608));
}

TEST_CASE("AC-04-5：分段轉換與一次轉換的結果逐位元相同") {
    const quint32 dsdRate = GENERATE(2822400u, 11289600u);
    CAPTURE(dsdRate);
    DsdTone tone(dsdRate, 1000.0, 0.3);
    const QByteArray dsd = stereoToneDsd(tone, 20000);
    PcmConvertSink whole;
    openStereo(whole, dsdRate);
    const auto expected = convertAll(whole, dsd);

    PcmConvertSink pieces;
    openStereo(pieces, dsdRate);
    std::vector<std::int32_t> got;
    qsizetype at = 0;
    quint32 seed = 7;
    while (at < dsd.size() / 8) {
        // 1–997 框不等長（含奇數框：DSD256 一個輸出樣本跨兩個 DSD 樣本框）
        seed = seed * 1103515245u + 12345u;
        const qsizetype step = std::min<qsizetype>(1 + (seed >> 16) % 997, dsd.size() / 8 - at);
        const auto part = convertAll(pieces, dsd.mid(at * 8, step * 8));
        got.insert(got.end(), part.begin(), part.end());
        at += step;
    }
    CHECK(got == expected);
}

TEST_CASE("AC-04-5：DSD64 連續轉 60 秒，樣本數正確、沒有不連續與溢位") {
    constexpr double kAmplitude = 0.25;
    PcmConvertSink sink;
    openStereo(sink, 2822400);
    DsdTone tone(2822400, 1000.0, kAmplitude);
    // 1 kHz、振幅 0.25 在 176.4 kHz 下相鄰樣本最多差約 0.009；加上雜訊與測試音開頭調變器起步的
    // 小突波（實測約 0.026，是測試訊號本身的）以 0.04 為上限，掉樣本或重複一段時差值通常遠大於這個
    constexpr double kMaxStep = 0.04;
    constexpr qsizetype kChunk = 4096; // 和核心每次讀的框數相同
    const qsizetype total = 2822400 / 32 * 60;
    std::int32_t previous = 0;
    std::int32_t worstStep = 0;
    std::int32_t peak = 0;
    qint64 outFrames = 0;
    for (qsizetype at = 0; at < total; at += kChunk) {
        const auto samples = convertAll(sink, stereoToneDsd(tone, std::min(kChunk, total - at)));
        for (std::size_t i = 0; i < samples.size(); i += 2) {
            worstStep = std::max(worstStep, std::abs(samples[i] - previous));
            peak = std::max(peak, std::abs(samples[i]));
            previous = samples[i];
        }
        outFrames += static_cast<qint64>(samples.size() / 2);
    }
    CAPTURE(worstStep, peak);
    CHECK(outFrames == 176400LL * 60);
    CHECK(worstStep < static_cast<std::int32_t>(kMaxStep * 8388608));
    CHECK(peak < static_cast<std::int32_t>(kAmplitude * 1.02 * 8388608));
}

TEST_CASE("AC-04-6：轉 PCM 與 PCM 曲目相鄰時，換格式前後送的是接近 0 的靜音，接縫沒有突波") {
    // 曲目像真的 DSD 檔一樣頭尾各有 10 ms 的靜音碼，中間是 0.1 秒的測試音：整段（含換格式補的
    // 靜音）相鄰樣本的差都應該很小。曲目結尾不是靜音時，濾波器裡最後約 0.08 ms 會被核心補的靜音
    // 取代（包裝器沒有排空的入口），見模組規格的轉 PCM 段
    DsdTone tone(2822400, 1000.0, 0.25);
    const QByteArray edge(2822400 / 32 / 100 * 8, kDsdSilence);
    const QByteArray dsd = edge + stereoToneDsd(tone, 2822400 / 32 / 10) + edge;
    const QByteArray pcm(4410 * 4, '\0');
    TrackList tracks;
    tracks.add(std::make_unique<MemorySource>(pcmFormat(44100, 16), pcm), 2);
    tracks.add(std::make_unique<MemorySource>(dsdAudio(2822400), dsd), 3);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<PcmConvertSink>());
    engine.setNextTrackProvider(&tracks);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine
                .play(EngineTrack{std::make_unique<MemorySource>(dsdAudio(2822400), dsd), 0, -1, 1},
                      capture)
                .hasValue());
    REQUIRE(playToEnd(engine));
    CHECK_FALSE(engine.decodeError().has_value());
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 3); // DSD → PCM → DSD
    const auto silence = static_cast<qsizetype>(176400 * kDopSilenceMs / 1000 * 2);
    for (const qsizetype s : {0, 2}) {
        CAPTURE(s);
        CHECK(sessions[s].format == pcmFormat(176400, 24));
        const auto samples = unpack24in32(sessions[s].data);
        REQUIRE(static_cast<qsizetype>(samples.size()) > silence * 2);
        // 換格式的那一側至少 50 ms 接近 0：換走前的結尾（第 1 段）、換進來的開頭（第 3 段）
        if (s == 0) {
            CHECK(maxAbs(std::vector<std::int32_t>(samples.end() - silence, samples.end())) <=
                  kSilenceLsb);
        } else {
            CHECK(maxAbs(std::vector<std::int32_t>(samples.begin(), samples.begin() + silence)) <=
                  kSilenceLsb);
        }
        std::int32_t worstStep = 0;
        for (std::size_t i = 2; i < samples.size(); ++i) {
            worstStep = std::max(worstStep, std::abs(samples[i] - samples[i - 2]));
        }
        CAPTURE(worstStep);
        // 上限同 60 秒的測試（含測試音開頭調變器起步的小突波）
        CHECK(worstStep < static_cast<std::int32_t>(0.04 * 8388608));
        CHECK(maxAbs(samples) > static_cast<std::int32_t>(0.2 * 8388608)); // 曲目確實在裡面
    }
}
