// T-87：DSD 轉 PCM 時開放音量（AC-02-7、AC-04-5）。包裝器轉出的是一般 PCM，音量接在它之後；
// DoP 與原生 DSD 送的是位元流本身，照舊不能調。用假的包裝器與 Capture 後端做位元比對。
#include "output_fixture.h"

#include "../audio/volume_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

#include <cstring>

using auralis::DsdFormat;
using auralis::core::PlaybackEngine;
using auralis::test::lsb;
using auralis::test::sampleAt;
using auralis::test::waitFinished;

namespace {

AudioFormat dsd64() {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

/// 假的包裝器：每個 DSD 樣本框轉成一個 24-in-32 的 PCM 樣本框（每聲道的 4 個 DSD 位元組
/// 當成小端整數、最低位元組清成 0），內容可以從輸入直接算出來。
/// reason 是空字串時當成 DoP（輸出雖然是 PCM 的格式，內容是位元流），非空時當成轉 PCM。
class TestSink final : public auralis::DsdSink {
public:
    explicit TestSink(QString reason) : m_reason(std::move(reason)) {}
    QString name() const override { return m_reason.isEmpty() ? QStringLiteral("DoP") : m_reason; }
    auralis::Result<AudioFormat> open(const DsdFormat& format) override {
        m_frameBytes = format.bytesPerFrame();
        AudioFormat out;
        out.rate = format.dsdRate / 32;
        out.bits = 24;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        const auto bytes = static_cast<std::size_t>(dsdFrames * m_frameBytes);
        std::memcpy(out, dsd, bytes);
        for (std::size_t i = 0; i < bytes; i += 4) {
            out[i] = std::byte{0};
        }
        return dsdFrames;
    }
    QString reason() const override { return m_reason; }

private:
    QString m_reason;
    int m_frameBytes = 0;
};

const QString kToPcm = QStringLiteral("DSD 轉 PCM");

/// 隨機的 DSD 位元流（frames 個樣本框）。
QByteArray randomDsd(qsizetype frames) {
    QByteArray dsd(frames * dsd64().bytesPerFrame(), Qt::Uninitialized);
    quint32 state = 4242;
    for (char& byte : dsd) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<char>(state >> 24);
    }
    return dsd;
}

/// TestSink 對 dsd 轉出的 PCM。
QByteArray sinkOutput(QByteArray dsd) {
    for (qsizetype i = 0; i < dsd.size(); i += 4) {
        dsd[i] = 0;
    }
    return dsd;
}

TrackRef addDsd(Fixture& f, const QString& name, double seconds) {
    const QString path = name + QStringLiteral(".mem");
    const AudioFormat format = dsd64();
    const auto frames = static_cast<qsizetype>(format.dsdRate / 32 * seconds);
    f.sourceFactory->tracks[path] = {format, QByteArray(frames * format.bytesPerFrame(), '\x69')};
    return TrackRef{path, 0};
}

} // namespace

TEST_CASE("AC-02-7（T-87）：DSD 轉 PCM 時音量可用，作用在包裝器轉出的 PCM 之後") {
    const QByteArray dsd = randomDsd(22050);
    const QByteArray pcm = sinkOutput(dsd);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<TestSink>(kToPcm));
    engine.setVolume(50);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(dsd64(), dsd), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().volumeAvailable);
    CHECK_FALSE(status.value().bitPerfect);
    CHECK(status.value().reasons == QStringList{kToPcm, QStringLiteral("音量 50%")});
    REQUIRE(waitFinished(engine));
    CHECK(engine.underruns() == 0);

    const AudioFormat format = status.value().outputFormat;
    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= pcm.size());
    const qint64 unit = lsb(format);
    qint64 worst = 0;
    for (qsizetype i = 0; i < pcm.size() / 4; ++i) {
        // 增益 0.125：四捨五入加 TPDF 抖動，最多差 2 個最低位元
        const double expected = static_cast<double>(sampleAt(pcm, format, i)) * 0.125;
        const double error = std::abs(static_cast<double>(sampleAt(output, format, i)) - expected);
        worst = std::max(worst, static_cast<qint64>(std::ceil(error)));
    }
    CHECK(worst <= 2 * unit);
}

TEST_CASE(
    "AC-02-1（T-87）：DSD 轉 PCM、音量 100% 時輸出和包裝器轉出的逐位元相同，只列「DSD 轉 PCM」") {
    const QByteArray dsd = randomDsd(8820);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<TestSink>(kToPcm));
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(dsd64(), dsd), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().volumeAvailable);
    CHECK(status.value().reasons == QStringList{kToPcm});
    REQUIRE(waitFinished(engine));
    CHECK(capture.captured().left(dsd.size()) == sinkOutput(dsd));
}

TEST_CASE("AC-02-7（T-87）：DSD 轉 PCM、音量 0% 時是數位靜音") {
    const QByteArray dsd = randomDsd(8820);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<TestSink>(kToPcm));
    engine.setVolume(0);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(dsd64(), dsd), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().reasons == QStringList{kToPcm, QStringLiteral("音量 0%")});
    REQUIRE(waitFinished(engine));
    CHECK(capture.captured().left(dsd.size()) == QByteArray(dsd.size(), '\0'));
}

TEST_CASE("AC-02-7（T-87）：DoP 輸出的格式像 PCM，音量照樣不可用、位元流不變") {
    const QByteArray dsd = randomDsd(8820);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<TestSink>(QString()));
    engine.setVolume(30);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(dsd64(), dsd), capture);
    REQUIRE(status.hasValue());
    CHECK_FALSE(status.value().volumeAvailable);
    CHECK(status.value().bitPerfect);
    CHECK(status.value().reasons.isEmpty());
    REQUIRE(waitFinished(engine));
    CHECK(capture.captured().left(dsd.size()) == sinkOutput(dsd));
}

TEST_CASE(
    "AC-02-7（T-87）：播放中切換 DoP 與轉 PCM 時發出 volumeAvailableChanged，記下的音量跟著作用") {
    Fixture f;
    f.outputs.setDsdSink(std::make_shared<TestSink>(QString()));
    const TrackRef a = addDsd(f, QStringLiteral("a"), 6.0);
    f.service.setVolume(40);
    QSignalSpy available(&f.service, &PlaybackService::volumeAvailableChanged);
    f.service.playFrom({a}, 0);
    REQUIRE(f.service.state() == State::Playing);
    CHECK_FALSE(f.service.volumeAvailable());
    CHECK(f.service.volumeUnavailableReason() == QStringLiteral("DSD 直通時無法調整音量"));
    REQUIRE(available.size() == 1);
    CHECK_FALSE(available.at(0).at(0).toBool());
    CHECK(f.service.outputStatus().reasons.isEmpty());

    // 進階輸出模組換 DSD 輸出方式的做法：暫停、換包裝器、從原位置繼續
    const auto switchSink = [&](const QString& reason) {
        f.service.pause();
        f.outputs.setDsdSink(std::make_shared<TestSink>(reason));
        f.service.play();
        REQUIRE(f.service.state() == State::Playing);
    };
    switchSink(kToPcm);
    CHECK(f.service.volumeAvailable());
    CHECK(f.service.volumeUnavailableReason().isEmpty());
    REQUIRE(available.size() == 2);
    CHECK(available.at(1).at(0).toBool());
    CHECK(f.service.outputStatus().reasons == QStringList{kToPcm, QStringLiteral("音量 40%")});
    // 轉 PCM 時調音量立刻反映在輸出狀態
    f.service.setVolume(100);
    CHECK(f.service.outputStatus().reasons == QStringList{kToPcm});
    f.service.setVolume(60);

    switchSink(QString());
    CHECK_FALSE(f.service.volumeAvailable());
    REQUIRE(available.size() == 3);
    CHECK_FALSE(available.at(2).at(0).toBool());
    CHECK(f.service.outputStatus().reasons.isEmpty());
    CHECK(f.service.volume() == 60);
    f.service.stop();
}
