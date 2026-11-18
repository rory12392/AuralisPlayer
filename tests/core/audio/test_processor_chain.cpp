// 處理器鏈與 DSD 包裝器經過播放引擎的行為（Architecture §5、§8 的處理器規則）。
#include "engine_helpers.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <QtEndian>

#include <cstdlib>
#include <random>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::IProcessor;
using auralis::IProcessorFactory;
using auralis::Result;
using auralis::core::CaptureBackend;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::waitFinished;

namespace {

/// 乘上固定增益的處理器；增益 1.0 也算作用中（用來檢查浮點往返與抖動只差 1 個最低位元）。
class GainProcessor final : public IProcessor {
public:
    explicit GainProcessor(double gain) : m_gain(gain) {}
    void prepare(const AudioFormat&) override {}
    bool isActive() const noexcept override { return true; }
    QString reason() const override { return QStringLiteral("測試增益已套用"); }
    void process(double* samples, qint64 frames, int channels) noexcept override {
        for (qint64 i = 0; i < frames * channels; ++i) {
            samples[i] *= m_gain;
        }
    }

private:
    double m_gain;
};

class GainFactory final : public IProcessorFactory {
public:
    explicit GainFactory(double gain) : m_gain(gain) {}
    QString name() const override { return QStringLiteral("gain"); }
    std::unique_ptr<IProcessor> create() override {
        return std::make_unique<GainProcessor>(m_gain);
    }

private:
    double m_gain;
};

AudioFormat pcm24() {
    AudioFormat format;
    format.rate = 48000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    return format;
}

qint32 sampleAt(const QByteArray& bytes, qsizetype index) {
    return qFromLittleEndian<qint32>(bytes.constData() + index * 4);
}

/// 測試用的 DoP 包裝器：每個 DSD 樣本框（每聲道 32 位元）變成兩個 24 位元樣本框，
/// 上 8 位元是 0x05／0xFA 交替的標記、接著 16 個 DSD 位元。
class TestDopSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("DoP（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
        m_channels = format.channels;
        AudioFormat out;
        out.rate = format.dsdRate / 16;
        out.bits = 24;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames * 2; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        return pack(dsd, dsdFrames, m_channels, out, m_marker);
    }

    static qint64 pack(const std::byte* dsd, qint64 frames, int channels, std::byte* out,
                       bool& marker) noexcept {
        auto* samples = reinterpret_cast<quint32*>(out);
        for (qint64 f = 0; f < frames; ++f) {
            for (int half = 0; half < 2; ++half) {
                const quint32 mark = marker ? 0xFAu : 0x05u;
                for (int ch = 0; ch < channels; ++ch) {
                    const auto* bytes =
                        reinterpret_cast<const quint8*>(dsd) + (f * channels + ch) * 4;
                    *samples++ = (mark << 24) | (quint32{bytes[half * 2]} << 16) |
                                 (quint32{bytes[half * 2 + 1]} << 8);
                }
                marker = !marker;
            }
        }
        return frames * 2;
    }

private:
    int m_channels = 0;
    bool m_marker = false;
};

AudioFormat dsd64() {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

} // namespace

TEST_CASE("AC-02-3：處理器作用時改走浮點路徑，狀態不是位元完美並附原因") {
    const AudioFormat format = pcm24();
    const QByteArray input = makeNoise(format, 12000, 7);
    ProcessorChainImpl chain;
    chain.add(std::make_shared<GainFactory>(0.5));
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, input), capture);
    REQUIRE(status.hasValue());
    CHECK_FALSE(status.value().bitPerfect);
    CHECK(status.value().reasons == QStringList{QStringLiteral("測試增益已套用")});
    REQUIRE(waitFinished(engine));

    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    for (qsizetype i = 0; i < input.size() / 4; ++i) {
        // 增益 0.5 之後捨入到 24 位元並抖動：和輸入的一半最多差 2 個最低位元（512）
        const qint64 expected = sampleAt(input, i) / 2;
        const qint64 diff = sampleAt(output, i) - expected;
        INFO(i);
        REQUIRE((diff >= -512 && diff <= 512));
        REQUIRE((sampleAt(output, i) & 0xFF) == 0);
    }
}

TEST_CASE("處理器：增益 1.0 也走浮點與 TPDF 抖動，結果只差 1 個最低位元") {
    AudioFormat format = pcm24();
    format.bits = 16;
    format.container = 16;
    const QByteArray input = makeNoise(format, 8000, 11);
    ProcessorChainImpl chain;
    chain.add(std::make_shared<GainFactory>(1.0));
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(std::make_unique<MemorySource>(format, input), capture).hasValue());
    REQUIRE(waitFinished(engine));

    const QByteArray output = capture.captured();
    REQUIRE(output.size() >= input.size());
    int changed = 0;
    for (qsizetype i = 0; i < input.size() / 2; ++i) {
        const qint16 a = qFromLittleEndian<qint16>(input.constData() + i * 2);
        const qint16 b = qFromLittleEndian<qint16>(output.constData() + i * 2);
        REQUIRE(std::abs(int{a} - int{b}) <= 1);
        changed += a != b ? 1 : 0;
    }
    // 抖動真的有加上去（不是被跳過的直通）
    CHECK(changed > 0);
}

TEST_CASE("DSD 經包裝器送到後端，不經過處理器鏈") {
    const AudioFormat format = dsd64();
    // DSD64 的一個樣本框是每聲道 32 個位元：0.1 秒約 8820 框，內容是隨機位元
    QByteArray dsd(8820 * format.bytesPerFrame(), Qt::Uninitialized);
    std::mt19937 random(3);
    for (char& byte : dsd) {
        byte = static_cast<char>(random() & 0xFF);
    }
    QByteArray expected(dsd.size() * 2, Qt::Uninitialized);
    bool marker = false;
    TestDopSink::pack(reinterpret_cast<const std::byte*>(dsd.constData()), 8820, 2,
                      reinterpret_cast<std::byte*>(expected.data()), marker);

    ProcessorChainImpl chain;
    chain.add(std::make_shared<GainFactory>(0.5)); // 作用中也不能碰 DSD
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<TestDopSink>());
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::make_unique<MemorySource>(format, dsd), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().bitPerfect);
    CHECK(status.value().outputFormat.rate == 176400);
    REQUIRE(waitFinished(engine));
    CHECK(engine.underruns() == 0);
    // 播完後補的是 DoP 靜音封包（靜音碼 0x69），標記接著曲目最後一框交替（AC-04-6）
    const QByteArray captured = capture.captured();
    REQUIRE(captured.size() >= expected.size());
    const qsizetype tailBytes = captured.size() - expected.size();
    const qint64 silenceFrames = tailBytes / 16 + 1;
    const QByteArray silence(silenceFrames * format.bytesPerFrame(), '\x69');
    QByteArray tail(silenceFrames * 16, Qt::Uninitialized);
    TestDopSink::pack(reinterpret_cast<const std::byte*>(silence.constData()), silenceFrames, 2,
                      reinterpret_cast<std::byte*>(tail.data()), marker);
    CHECK(captured.left(expected.size()) == expected);
    CHECK(captured.mid(expected.size()) == tail.left(tailBytes));
}

TEST_CASE("DSD：沒有包裝器時回傳錯誤，引擎維持停止") {
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    auto status =
        engine.play(std::make_unique<MemorySource>(dsd64(), QByteArray(800, '\x69')), capture);
    REQUIRE_FALSE(status.hasValue());
    CHECK(status.error().message.contains(QStringLiteral("DSD")));
    CHECK_FALSE(engine.isPlaying());
}
