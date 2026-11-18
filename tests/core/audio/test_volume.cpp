// 音量（AC-02-7）經過播放引擎的行為，都用 Capture 後端比對：音量曲線；100% 逐位元相同；
// 其他值是「樣本 × 增益」加抖動；0% 是數位靜音；DSD 不受音量影響。
// 播放中改音量的測試在 test_volume_live.cpp。
#include "engine_helpers.h"
#include "volume_probe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstring>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::Result;
using auralis::core::CaptureBackend;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::volumeGain;
using auralis::test::fastCapture;
using auralis::test::lsb;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::pcmFormat;
using auralis::test::sampleAt;
using auralis::test::startsWithThenSilence;
using auralis::test::waitFinished;

namespace {

/// 測試用的 DSD 包裝器：位元流原樣放進 32 位元容器（直通）。
class PassThroughSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("直通（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
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

} // namespace

TEST_CASE("AC-02-7：音量曲線是三次曲線，100% 是 1.0、0% 是靜音、越大越大聲") {
    CHECK(volumeGain(100) == 1.0);
    CHECK(volumeGain(0) == 0.0);
    CHECK(std::abs(volumeGain(50) - 0.125) < 1e-12);
    CHECK(std::abs(20.0 * std::log10(volumeGain(50)) + 18.06) < 0.01);
    CHECK(std::abs(20.0 * std::log10(volumeGain(10)) + 60.0) < 0.01);
    for (int percent = 1; percent <= 100; ++percent) {
        CHECK(volumeGain(percent) > volumeGain(percent - 1));
    }
    // 超出範圍的值取到 0–100
    CHECK(volumeGain(140) == 1.0);
    CHECK(volumeGain(-5) == 0.0);
}

TEST_CASE("AC-02-1：音量 100% 時送到裝置緩衝的資料逐位元相同，調低再調回 100% 也一樣") {
    const AudioFormat format = GENERATE(pcmFormat(44100, 16), pcmFormat(96000, 24));
    CAPTURE(format.rate, format.bits);
    const QByteArray input = makeNoise(format, static_cast<int>(format.rate / 2), 21);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    // 還沒播的時候調過音量，回到 100% 之後不留下任何處理
    engine.setVolume(35);
    engine.setVolume(100);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, input), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().bitPerfect);
    CHECK(status.value().reasons.isEmpty());
    CHECK(status.value().volumeAvailable);
    REQUIRE(waitFinished(engine));
    CHECK(engine.underruns() == 0);
    CHECK(startsWithThenSilence(capture.captured(), input));
}

TEST_CASE("AC-02-7：音量 50% 時每個樣本是原樣本乘上增益，誤差在抖動範圍內，狀態寫出「音量 50%」") {
    const AudioFormat format =
        GENERATE(pcmFormat(44100, 16), pcmFormat(48000, 24), pcmFormat(192000, 24));
    CAPTURE(format.rate, format.bits);
    const QByteArray input = makeNoise(format, static_cast<int>(format.rate / 4), 9);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setVolume(50);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, input), capture);
    REQUIRE(status.hasValue());
    CHECK_FALSE(status.value().bitPerfect);
    CHECK(status.value().reasons == QStringList{QStringLiteral("音量 50%")});
    REQUIRE(waitFinished(engine));
    CHECK(engine.underruns() == 0);

    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    const qint64 unit = lsb(format);
    const qint64 samples = input.size() / (format.container / 8);
    qint64 worst = 0;
    bool lowBitsClear = true;
    for (qint64 i = 0; i < samples; ++i) {
        // 增益 0.125：四捨五入加 TPDF 抖動（±1 個最低位元），最多差 2 個最低位元
        const double expected = static_cast<double>(sampleAt(input, format, i)) * 0.125;
        const qint64 actual = sampleAt(output, format, i);
        const double error = std::abs(static_cast<double>(actual) - expected);
        worst = std::max(worst, static_cast<qint64>(std::ceil(error)));
        lowBitsClear = lowBitsClear && (actual % unit) == 0;
    }
    CHECK(worst <= 2 * unit);
    // 抖動真的有加（不是單純截斷）：誤差超過半個最低位元
    CHECK(worst > unit / 2);
    // 24 位元放在 32 位元容器裡，低 8 位元仍然是 0
    CHECK(lowBitsClear);
}

TEST_CASE("AC-02-7：音量 0% 是數位靜音，狀態寫出「音量 0%」") {
    const AudioFormat format = pcmFormat(48000, 24);
    const QByteArray input = makeNoise(format, 12000, 4);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setVolume(0);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, input), capture);
    REQUIRE(status.hasValue());
    CHECK_FALSE(status.value().bitPerfect);
    CHECK(status.value().reasons == QStringList{QStringLiteral("音量 0%")});
    REQUIRE(waitFinished(engine));
    CHECK(capture.sourceFrames() == 12000);
    // 全部是 0（沒有抖動雜訊）
    CHECK(startsWithThenSilence(capture.captured(), QByteArray()));
}

TEST_CASE("AC-02-7：DSD 直通時音量不可用，位元流不受音量影響") {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    QByteArray dsd(8820 * format.bytesPerFrame(), Qt::Uninitialized);
    quint32 state = 12345;
    for (char& byte : dsd) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<char>(state >> 24);
    }
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<PassThroughSink>());
    engine.setVolume(30);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, dsd), capture);
    REQUIRE(status.hasValue());
    CHECK_FALSE(status.value().volumeAvailable);
    // 音量沒有作用在位元流上，所以不列為改變訊號的原因
    CHECK(status.value().bitPerfect);
    CHECK(status.value().reasons.isEmpty());
    engine.setVolume(10);
    REQUIRE(waitFinished(engine));
    // 播完後補的是包裝器轉換過的 DSD 靜音碼 0x69（直通包裝器原樣送出，AC-04-6）
    const QByteArray captured = capture.captured();
    CHECK(captured.left(dsd.size()) == dsd);
    CHECK(captured.mid(dsd.size()) == QByteArray(captured.size() - dsd.size(), '\x69'));
}
