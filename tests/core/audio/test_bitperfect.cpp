// 位元完美（bitperfect）：解碼後的 PCM 經播放引擎送到裝置緩衝（Capture 後端）要逐位元相同。
// 測試訊號在測試時產生並用 FFmpeg 編成 FLAC（.localdata\tmp\ 底下），再由核心的 FFmpeg 音源解碼。
#include "audio/ffmpeg_source.h"
#include "engine_helpers.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>

using auralis::AudioFormat;
using auralis::IProcessor;
using auralis::IProcessorFactory;
using auralis::core::CaptureBackend;
using auralis::core::FfmpegSourceFactory;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::encodeFile;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::startsWithThenSilence;
using auralis::test::waitFinished;

namespace {

struct Case {
    quint32 rate;
    quint16 bits;
};

/// 永遠不作用的處理器（例：ReplayGain 關閉）：鏈上有它也要位元完美。
class IdleProcessor final : public IProcessor {
public:
    void prepare(const AudioFormat&) override {}
    bool isActive() const noexcept override { return false; }
    QString reason() const override { return QStringLiteral("不該出現"); }
    void process(double* samples, qint64 frames, int channels) noexcept override {
        for (qint64 i = 0; i < frames * channels; ++i) {
            samples[i] = 0.0;
        }
    }
};

class IdleFactory final : public IProcessorFactory {
public:
    QString name() const override { return QStringLiteral("idle"); }
    std::unique_ptr<IProcessor> create() override { return std::make_unique<IdleProcessor>(); }
};

/// 產生 0.5 秒雜訊、用 FFmpeg 編成檔案（codec／muxer 是 FFmpeg 的名稱，副檔名同 muxer），
/// 回傳原始 PCM 與檔案路徑。
QString makeEncoded(const AudioFormat& format, const char* codec, const char* muxer,
                    QByteArray& pcm) {
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t05-bitperfect");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/noise-%1-%2.%3")
                                   .arg(format.rate)
                                   .arg(format.bits)
                                   .arg(QLatin1String(muxer));
    pcm = makeNoise(format, static_cast<int>(format.rate / 2), format.rate + format.bits);
    const QString why = encodeFile(path, codec, muxer, format, pcm);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    return path;
}

AudioFormat formatOf(const Case& c) {
    AudioFormat format;
    format.rate = c.rate;
    format.bits = c.bits;
    format.container = c.bits == 16 ? 16 : 32;
    format.channels = 2;
    return format;
}

/// 檔案經核心 FFmpeg 音源 → 播放引擎（鏈上掛一個不作用的處理器）→ Capture，
/// 送到裝置緩衝的資料開頭要逐位元等於 pcm、之後全是 0。
void checkBitPerfect(const QString& path, const AudioFormat& format, const QByteArray& pcm) {
    FfmpegSourceFactory sources;
    auto source = sources.open(path);
    REQUIRE(source.hasValue());
    REQUIRE(source.value()->format() == format);

    ProcessorChainImpl chain;
    chain.add(std::make_shared<IdleFactory>());
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    auto status = engine.play(std::move(source).value(), capture);
    REQUIRE(status.hasValue());
    CHECK(status.value().bitPerfect);
    CHECK(status.value().reasons.isEmpty());
    CHECK(status.value().deviceFormat == format);

    REQUIRE(waitFinished(engine));
    CHECK_FALSE(engine.decodeError().has_value());
    CHECK(engine.underruns() == 0);
    CHECK(capture.sourceFrames() == pcm.size() / format.bytesPerFrame());
    CHECK(startsWithThenSilence(capture.captured(), pcm));
}

} // namespace

TEST_CASE("AC-02-1：位元完美（bitperfect）解碼後的 PCM 與送到裝置緩衝的資料逐位元相同") {
    const auto c = GENERATE(Case{44100, 16}, Case{48000, 16}, Case{96000, 16}, Case{192000, 16},
                            Case{44100, 24}, Case{48000, 24}, Case{96000, 24}, Case{192000, 24});
    const AudioFormat format = formatOf(c);
    CAPTURE(c.rate, c.bits);

    QByteArray pcm;
    const QString path = makeEncoded(format, "flac", "flac", pcm);
    checkBitPerfect(path, format, pcm);
}

TEST_CASE("AC-02-1：位元完美（bitperfect）WAV 經播放引擎送到裝置緩衝的資料逐位元相同") {
    const auto c = GENERATE(Case{44100, 16}, Case{48000, 16}, Case{96000, 16}, Case{192000, 16},
                            Case{44100, 24}, Case{48000, 24}, Case{96000, 24}, Case{192000, 24});
    const AudioFormat format = formatOf(c);
    CAPTURE(c.rate, c.bits);

    QByteArray pcm;
    const QString path = makeEncoded(format, c.bits == 16 ? "pcm_s16le" : "pcm_s24le", "wav", pcm);
    checkBitPerfect(path, format, pcm);
}
