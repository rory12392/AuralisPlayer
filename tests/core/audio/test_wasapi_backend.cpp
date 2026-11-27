// WASAPI 後端經過播放引擎的行為：格式協商（AC-02-2）、被佔用與拔除（AC-02-4）、共享模式。
// 用假的 IAudioClient，不開實體裝置。
#include "engine_helpers.h"
#include "fake_wasapi.h"
#include "signal_writer.h"

#include "audio/wasapi_backend.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <thread>

using auralis::AudioFormat;
using auralis::OutputErrorKind;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::WasapiBackend;
using auralis::core::WasapiMode;
using auralis::test::FakeDevice;
using auralis::test::FakeEndpoints;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using auralis::test::supportsOnly;
using auralis::test::waitFinished;

namespace {

AudioFormat pcm(quint32 rate, quint16 bits, quint16 container, quint16 channels = 2) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = container;
    format.channels = channels;
    return format;
}

/// 一個假裝置加上接著它的後端與引擎。
struct Rig {
    explicit Rig(WasapiMode mode = WasapiMode::Exclusive)
        : device(std::make_shared<FakeDevice>()),
          backend(mode, std::make_shared<FakeEndpoints>(device), device->id), engine(chain) {}

    auralis::Result<PlaybackEngine::Status> play(const AudioFormat& format,
                                                 const QByteArray& data) {
        return engine.play(std::make_unique<MemorySource>(format, data), backend);
    }

    std::shared_ptr<FakeDevice> device;
    WasapiBackend backend;
    ProcessorChainImpl chain;
    PlaybackEngine engine;
};

/// 24-in-32（左對齊）→ 每個樣本取高 3 位元組。
QByteArray packTo24(const QByteArray& data) {
    QByteArray packed;
    for (qsizetype i = 0; i + 4 <= data.size(); i += 4) {
        packed.append(data.mid(i + 1, 3));
    }
    return packed;
}

bool waitForOutputError(PlaybackEngine& engine, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!engine.outputError() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return engine.outputError().has_value();
}

} // namespace

TEST_CASE("AC-02-2：24 位元來源在只收 24-in-32 的裝置上用 32 位元容器以原取樣率獨占開啟，"
          "送出的資料逐位元相同") {
    Rig rig;
    rig.device->supports = supportsOnly({{96000, 32, 24}});
    const AudioFormat format = pcm(96000, 24, 32);
    const QByteArray data = makeNoise(format, 48000, 7);
    auto played = rig.play(format, data);
    REQUIRE(played.hasValue());
    CHECK(played.value().deviceFormat == format);
    CHECK(played.value().bitPerfect);
    REQUIRE(waitFinished(rig.engine));
    CHECK(startsWithThenSilence(rig.device->writtenData(), data));
    const WAVEFORMATEXTENSIBLE& wave = rig.device->initialized.back();
    CHECK(wave.Format.nSamplesPerSec == 96000);
    CHECK(wave.Format.wBitsPerSample == 32);
    CHECK(wave.Samples.wValidBitsPerSample == 24);
    CHECK(rig.device->modes.back() == AUDCLNT_SHAREMODE_EXCLUSIVE);
    CHECK((rig.device->flags.back() & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) != 0);
}

TEST_CASE("AC-02-2：裝置收 24-in-24 時優先用它，送出的是每個樣本原封不動的 24 位元") {
    Rig rig;
    rig.device->supports = supportsOnly({{44100, 24, 24}, {44100, 32, 24}});
    const AudioFormat format = pcm(44100, 24, 32);
    const QByteArray data = makeNoise(format, 22050, 3);
    auto played = rig.play(format, data);
    REQUIRE(played.hasValue());
    CHECK(played.value().deviceFormat.container == 24);
    REQUIRE(waitFinished(rig.engine));
    CHECK(startsWithThenSilence(rig.device->writtenData(), packTo24(data)));
    CHECK(rig.device->initialized.back().Format.wBitsPerSample == 24);
}

TEST_CASE("AC-02-2：裝置不支援來源的取樣率時回報格式不支援，不改用別的取樣率") {
    Rig rig;
    rig.device->supports = supportsOnly({{48000, 24, 24}, {48000, 32, 24}});
    auto played = rig.play(pcm(44100, 24, 32), makeNoise(pcm(44100, 24, 32), 4410, 1));
    REQUIRE_FALSE(played.hasValue());
    CHECK(rig.backend.lastErrorKind() == OutputErrorKind::FormatNotSupported);
    CHECK(played.error().message.contains(QStringLiteral("44100")));
    for (const auto& wave : rig.device->initialized) {
        CHECK(wave.Format.nSamplesPerSec == 44100);
    }
    CHECK_FALSE(rig.engine.isPlaying());
}

TEST_CASE("AC-02-2：下一首的取樣率不同時以新的取樣率重開裝置") {
    Rig rig;
    REQUIRE(rig.play(pcm(44100, 16, 16), makeNoise(pcm(44100, 16, 16), 4410, 1)).hasValue());
    REQUIRE(waitFinished(rig.engine));
    REQUIRE(rig.play(pcm(96000, 24, 32), makeNoise(pcm(96000, 24, 32), 9600, 2)).hasValue());
    REQUIRE(waitFinished(rig.engine));
    REQUIRE(rig.device->initialized.size() >= 2);
    CHECK(rig.device->initialized.front().Format.nSamplesPerSec == 44100);
    CHECK(rig.device->initialized.back().Format.nSamplesPerSec == 96000);
}

TEST_CASE("AC-02-4：裝置被別的程式佔用時回報被佔用、不當掉；釋放後再按播放可以播") {
    Rig rig;
    rig.device->initResult = AUDCLNT_E_DEVICE_IN_USE;
    const AudioFormat format = pcm(44100, 16, 16);
    const QByteArray data = makeNoise(format, 4410, 5);
    auto played = rig.play(format, data);
    REQUIRE_FALSE(played.hasValue());
    CHECK(rig.backend.lastErrorKind() == OutputErrorKind::DeviceBusy);
    CHECK(played.error().hresult == AUDCLNT_E_DEVICE_IN_USE);
    CHECK_FALSE(rig.engine.isPlaying());

    rig.device->initResult = S_OK;
    REQUIRE(rig.play(format, data).hasValue());
    REQUIRE(waitFinished(rig.engine));
    CHECK(startsWithThenSilence(rig.device->writtenData(), data));
}

TEST_CASE("AC-02-4：之前開啟失敗過，之後成功開啟與啟動時錯誤種類清回沒有錯誤") {
    Rig rig;
    rig.device->initResult = AUDCLNT_E_DEVICE_IN_USE;
    const AudioFormat format = pcm(44100, 16, 16);
    REQUIRE_FALSE(rig.backend.open(format).hasValue());
    REQUIRE(rig.backend.lastErrorKind() == OutputErrorKind::DeviceBusy);

    rig.device->initResult = S_OK;
    REQUIRE(rig.play(format, makeNoise(format, 4410, 8)).hasValue());
    CHECK(rig.backend.lastErrorKind() == OutputErrorKind::Other);
    REQUIRE(waitFinished(rig.engine));
}

TEST_CASE("AC-02-4：播放中裝置被拔除時停止取資料並回報裝置不見；接回後再按播放可以播") {
    Rig rig;
    rig.device->unplugAfterBuffers = 5;
    const AudioFormat format = pcm(48000, 16, 16);
    REQUIRE(rig.play(format, makeNoise(format, 96000, 9)).hasValue());
    REQUIRE(waitForOutputError(rig.engine));
    CHECK(rig.engine.outputError()->kind == OutputErrorKind::DeviceLost);
    CHECK(rig.engine.outputError()->error.hresult == AUDCLNT_E_DEVICE_INVALIDATED);
    const qint64 rendered = rig.engine.renderedFrames();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(rig.engine.renderedFrames() == rendered);
    rig.engine.stop();

    // 還沒接回：再按播放回報裝置不見，不當掉
    REQUIRE_FALSE(rig.play(format, makeNoise(format, 4800, 1)).hasValue());
    CHECK(rig.backend.lastErrorKind() == OutputErrorKind::DeviceLost);

    // 接回後：同一個後端重新取裝置，可以播
    rig.device->unplugAfterBuffers = -1;
    rig.device->present = true;
    rig.device->writtenBytes = 0;
    const QByteArray data = makeNoise(format, 4800, 2);
    REQUIRE(rig.play(format, data).hasValue());
    REQUIRE(waitFinished(rig.engine));
    CHECK(startsWithThenSilence(rig.device->writtenData(), data));
}

TEST_CASE("獨占：緩衝大小沒對齊時換一個 IAudioClient 以對齊後的週期重新初始化") {
    Rig rig;
    rig.device->alignedFrames = 448;
    const AudioFormat format = pcm(44100, 16, 16);
    const QByteArray data = makeNoise(format, 4410, 4);
    REQUIRE(rig.play(format, data).hasValue());
    REQUIRE(waitFinished(rig.engine));
    REQUIRE(rig.device->periods.size() == 2);
    CHECK(rig.device->periods[1] == 101587); // 448 框 ÷ 44100 Hz，以 100 奈秒為單位
    CHECK(rig.device->activations == 2);
    CHECK(startsWithThenSilence(rig.device->writtenData(), data));
}

TEST_CASE("共享模式：以來源格式加上 Windows 自動轉換開啟（經混音器，獨占不能用時的備援）") {
    Rig rig(WasapiMode::Shared);
    rig.device->supports = supportsOnly({{48000, 32, 24}}); // 獨占的限制不影響共享
    const AudioFormat format = pcm(44100, 16, 16);
    const QByteArray data = makeNoise(format, 4410, 6);
    auto played = rig.play(format, data);
    REQUIRE(played.hasValue());
    CHECK(played.value().backend == QStringLiteral("WASAPI 共享"));
    REQUIRE(waitFinished(rig.engine));
    CHECK(rig.device->modes.back() == AUDCLNT_SHAREMODE_SHARED);
    const DWORD flags = rig.device->flags.back();
    CHECK((flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) != 0);
    CHECK((flags & AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM) != 0);
    CHECK(rig.device->initialized.back().Format.nSamplesPerSec == 44100);
    CHECK(startsWithThenSilence(rig.device->writtenData(), data));
}

TEST_CASE("獨占：浮點來源在不收浮點的裝置上轉成 32 位元整數（四捨五入、限制範圍）") {
    Rig rig;
    rig.device->supports = supportsOnly({{48000, 32, 32}});
    AudioFormat format = pcm(48000, 32, 32);
    format.isFloat = true;
    const float samples[] = {0.5f, -0.5f, 1.0f, -1.0f, 0.0f, 0.25f};
    const QByteArray data(reinterpret_cast<const char*>(samples), sizeof samples);
    auto played = rig.play(format, data);
    REQUIRE(played.hasValue());
    CHECK_FALSE(played.value().deviceFormat.isFloat);
    REQUIRE(waitFinished(rig.engine));
    const QByteArray written = rig.device->writtenData();
    REQUIRE(written.size() >= 24);
    qint32 out[6];
    std::memcpy(out, written.constData(), sizeof out);
    CHECK(out[0] == 1073741824);
    CHECK(out[1] == -1073741824);
    CHECK(out[2] == 2147483647);
    CHECK(out[3] == -2147483647 - 1);
    CHECK(out[4] == 0);
    CHECK(out[5] == 536870912);
}
