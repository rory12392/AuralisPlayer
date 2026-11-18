// 跳轉的落點精度（US-11）：用每個樣本都不同的雜訊，確認 seek 到第 n 個樣本框之後讀到的第一個
// 樣本框就是第 n 個（無損格式誤差 0），音源回報的長度等於實際的樣本框數；
// 再經播放引擎與 Capture 後端，確認送進裝置的第一個樣本框也是目標那一個。
// 檔案夠長、區塊夠大（FLAC 44.1 kHz 是 4096、192 kHz 是 16384
// 個樣本框一個區塊），和實際的音樂檔相近。
#include "audio/capture_backend.h"
#include "audio/ffmpeg_source.h"
#include "audio/playback_engine.h"
#include "engine_helpers.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using auralis::AudioFormat;
using auralis::IAudioSource;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::FfmpegSourceFactory;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::encodeFile;
using auralis::test::makeNoise;

namespace {

QString workDir() {
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/fix-seek-accuracy");
    QDir().mkpath(dir);
    return dir;
}

AudioFormat pcm(quint32 rate, quint16 bits) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    return format;
}

std::unique_ptr<IAudioSource> openSource(const QString& path) {
    FfmpegSourceFactory factory;
    auto source = factory.open(path);
    INFO((source ? QString() : source.error().message).toStdString());
    REQUIRE(source.hasValue());
    return std::move(source).value();
}

/// 讀 frames 個樣本框（到結尾就少一些）。
QByteArray readFrames(IAudioSource& source, qint64 frames) {
    const int frameBytes = source.format().bytesPerFrame();
    QByteArray out(frames * frameBytes, Qt::Uninitialized);
    qint64 done = 0;
    while (done < frames) {
        const auto read = source.read(reinterpret_cast<std::byte*>(out.data()) + done * frameBytes,
                                      frames - done);
        REQUIRE(read.hasValue());
        if (read.value() == 0) {
            break;
        }
        done += read.value();
    }
    out.truncate(done * frameBytes);
    return out;
}

/// seek 到 target 之後讀到的第一個樣本框，是原訊號的第幾個（和 target 的差；找不到回傳
/// INT64_MIN）。 雜訊的每個樣本框都不同，所以用開頭 64 個樣本框在原訊號裡的位置判斷。
qint64 landingError(IAudioSource& source, const QByteArray& expected, qint64 target) {
    const int frameBytes = source.format().bytesPerFrame();
    REQUIRE(source.seek(target).hasValue());
    const QByteArray got = readFrames(source, 2000);
    if (got == expected.mid(target * frameBytes, got.size())) {
        return 0;
    }
    const qsizetype at = expected.indexOf(got.left(64 * frameBytes));
    return at < 0 || at % frameBytes != 0 ? INT64_MIN : at / frameBytes - target;
}

struct LosslessCase {
    const char* name;
    const char* codec;
    const char* muxer;
    const char* suffix;
    quint32 rate;
    quint16 bits;
    int seconds;
    /// 編碼器的區塊大小（樣本框）；目標刻意選在區塊邊界前後。
    qint64 block;
};

/// 解到結尾，回傳第一個聲道（有損來源是 32 位元浮點）。
std::vector<float> decodeLeft(IAudioSource& source, qint64 maxFrames) {
    const int channels = source.format().channels;
    const QByteArray data = readFrames(source, maxFrames);
    const auto* samples = reinterpret_cast<const float*>(data.constData());
    std::vector<float> left(static_cast<std::size_t>(data.size() / (4 * channels)));
    for (std::size_t i = 0; i < left.size(); ++i) {
        left[i] = samples[i * channels];
    }
    return left;
}

/// 有損格式：seek 之後解出來的內容，對上從頭解碼的哪個位置（和 target 的差，樣本框）。
/// 解碼器剛 seek 完的前幾千個樣本還沒收斂（MP3 的位元儲存區、AAC 的重疊），所以跳過 skip 個再比。
qint64 lossyLandingError(IAudioSource& source, const std::vector<float>& reference, qint64 target,
                         qint64 skip, qint64 window) {
    REQUIRE(source.seek(target).hasValue());
    const std::vector<float> got = decodeLeft(source, skip + 2048);
    REQUIRE(got.size() == static_cast<std::size_t>(skip + 2048));
    qint64 best = INT64_MIN;
    double bestError = 1e300;
    for (qint64 lag = -window; lag <= window; ++lag) {
        const qint64 from = target + skip + lag;
        if (from < 0 || from + 2048 > static_cast<qint64>(reference.size())) {
            continue;
        }
        double error = 0;
        for (qint64 i = 0; i < 2048; ++i) {
            error += std::abs(got[static_cast<std::size_t>(skip + i)] -
                              reference[static_cast<std::size_t>(from + i)]);
        }
        if (error < bestError) {
            bestError = error;
            best = lag;
        }
    }
    return best;
}

/// 0.25 振幅的浮點雜訊（有損編碼器的輸入）。
QByteArray floatNoise(quint32 rate, int seconds, AudioFormat& format) {
    format = pcm(rate, 32);
    format.isFloat = true;
    QByteArray signal = makeNoise(format, static_cast<int>(rate) * seconds, 5);
    auto* samples = reinterpret_cast<float*>(signal.data());
    for (qsizetype i = 0; i < signal.size() / 4; ++i) {
        samples[i] *= 0.25f;
    }
    return signal;
}

} // namespace

TEST_CASE("US-11：無損格式 seek 到第 n 個樣本框，讀到的第一個就是第 n 個；長度等於實際樣本框數") {
    const auto c =
        GENERATE(LosslessCase{"FLAC 44.1/16", "flac", "flac", "flac", 44100, 16, 60, 4096},
                 LosslessCase{"FLAC 96/24", "flac", "flac", "flac", 96000, 24, 30, 8192},
                 LosslessCase{"FLAC 192/24", "flac", "flac", "flac", 192000, 24, 20, 16384},
                 LosslessCase{"WAV 44.1/16", "pcm_s16le", "wav", "wav", 44100, 16, 60, 4096},
                 LosslessCase{"ALAC 44.1/16", "alac", "ipod", "m4a", 44100, 16, 60, 4096},
                 LosslessCase{"WavPack 96/24", "wavpack", "wv", "wv", 96000, 24, 20, 4096});
    INFO(c.name);
    const AudioFormat format = pcm(c.rate, c.bits);
    const qint64 frames = static_cast<qint64>(c.rate) * c.seconds;
    const QByteArray expected = makeNoise(format, static_cast<int>(frames), 23);
    const QString path =
        workDir() +
        QStringLiteral("/lossless-%1-%2.%3").arg(c.rate).arg(c.bits).arg(QLatin1String(c.suffix));
    REQUIRE(encodeFile(path, c.codec, c.muxer, format, expected).isEmpty());

    auto source = openSource(path);
    CHECK(source->lengthFrames() == frames);

    std::vector<qint64> targets = {1,          c.block - 1,          c.block,      c.block + 1,
                                   frames / 2, frames - c.block - 1, frames - 2001};
    std::mt19937 random(7);
    for (int i = 0; i < 40; ++i) {
        targets.push_back(std::uniform_int_distribution<qint64>(0, frames - 2001)(random));
    }
    // 換成毫秒再換回來的目標（服務收到的是毫秒）
    for (const qint64 ms : {qint64{12345}, qint64{7001}, qint64{c.seconds * 1000 - 500}}) {
        targets.push_back(ms * c.rate / 1000);
    }
    qint64 worst = 0;
    for (const qint64 target : targets) {
        const qint64 error = landingError(*source, expected, target);
        CAPTURE(target, error);
        CHECK(error == 0);
        worst = std::max(worst, std::abs(error));
    }
    WARN(c.name << "：" << targets.size() << " 個目標，最大誤差 " << worst << " 個樣本框；長度 "
                << source->lengthFrames() << "（實際 " << frames << "）");
}

TEST_CASE("US-11：AAC（.m4a）seek 的落點和從頭解碼一致（誤差 0 個樣本框），長度等於實際樣本框數") {
    AudioFormat format;
    const int seconds = 60;
    const QByteArray signal = floatNoise(44100, seconds, format);
    const qint64 frames = 44100 * seconds;
    const QString path = workDir() + QStringLiteral("/lossy.m4a");
    REQUIRE(encodeFile(path, "aac", "ipod", format, signal).isEmpty());

    auto full = openSource(path);
    CHECK(full->lengthFrames() == frames);
    const std::vector<float> reference = decodeLeft(*full, frames + 44100);
    CHECK(static_cast<qint64>(reference.size()) == frames);

    auto source = openSource(path);
    qint64 worst = 0;
    for (const qint64 target : {qint64{1}, qint64{1023}, qint64{1024}, qint64{44100 * 7 + 333},
                                qint64{44100 * 31 + 5}, frames / 2, frames - 44100}) {
        const qint64 error = lossyLandingError(*source, reference, target, 4096, 4096);
        CAPTURE(target, error);
        CHECK(error == 0);
        worst = std::max(worst, std::abs(error));
    }
    WARN("AAC（.m4a）最大誤差 " << worst << " 個樣本框");
}

TEST_CASE("US-11：MP3（有 LAME 標頭）seek 的落點和從頭解碼一致（誤差 0 個樣本框）") {
    const QString path = QStringLiteral(AURALIS_FIXTURES "/audio/sine-440hz-1s.mp3");
    auto full = openSource(path);
    const std::vector<float> reference = decodeLeft(*full, 88200);
    REQUIRE(reference.size() == 44100);

    auto source = openSource(path);
    qint64 worst = 0;
    // 440 Hz 的週期約 100 個樣本框，只在 ±40 內找，才不會對到隔壁的週期
    for (const qint64 target :
         {qint64{1}, qint64{1151}, qint64{1152}, qint64{5000}, qint64{20000}, qint64{33333}}) {
        const qint64 error = lossyLandingError(*source, reference, target, 4096, 40);
        CAPTURE(target, error);
        CHECK(error == 0);
        worst = std::max(worst, std::abs(error));
    }
    WARN("MP3（LAME 標頭，1 秒）最大誤差 " << worst
                                           << " 個樣本框（只分得出 ±40 個樣本框內的位移）");
}

TEST_CASE("US-11：播放中跳轉，送進裝置的第一個樣本框就是目標那一個（FLAC、區段起點不為 0）") {
    struct EngineCase {
        const char* name;
        quint32 rate;
        quint16 bits;
        int seconds;
        /// 區段（CUE 曲目）的起點與跳轉目標（毫秒，從區段起點算）。
        qint64 startMs;
        qint64 targetMs;
    };
    const auto c = GENERATE(EngineCase{"44.1/16 整檔", 44100, 16, 20, 0, 12345},
                            EngineCase{"44.1/16 區段", 44100, 16, 20, 3210, 9876},
                            EngineCase{"192/24 區段", 192000, 24, 12, 2001, 6007});
    INFO(c.name);
    const AudioFormat format = pcm(c.rate, c.bits);
    const qint64 frames = static_cast<qint64>(c.rate) * c.seconds;
    const QByteArray expected = makeNoise(format, static_cast<int>(frames), 41);
    const QString path =
        workDir() + QStringLiteral("/engine-%1-%2.flac").arg(c.rate).arg(c.startMs);
    REQUIRE(encodeFile(path, "flac", "flac", format, expected).isEmpty());
    const int frameBytes = format.bytesPerFrame();
    const qint64 start = c.startMs * c.rate / 1000;
    const qint64 target = c.targetMs * c.rate / 1000;

    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend::Options options = auralis::test::fastCapture();
    options.maxCaptureSeconds = c.seconds;
    CaptureBackend backend(options);
    REQUIRE(engine.play(EngineTrack{openSource(path), start, -1, 5}, backend));
    while (engine.position().frames < c.rate / 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(engine.seek(target));
    // 位置從目標開始算，不會先回報別的值
    CHECK(engine.position().frames >= target);
    REQUIRE(auralis::test::waitFinished(engine));

    // 跳轉後送出的資料：找出目標之後某一段在記錄裡的位置，往回推就是跳轉後的第一個樣本框
    const QByteArray captured = backend.captured();
    const QByteArray tail = expected.mid((start + target) * frameBytes);
    const qsizetype at = captured.indexOf(tail.left(64 * frameBytes));
    REQUIRE(at > 0);
    REQUIRE(at % frameBytes == 0);
    CHECK(auralis::test::startsWithThenSilence(captured.mid(at), tail));
    // 它前面那個樣本框不是目標的前一個（是靜音或跳轉前的資料），所以第一個送出的就是目標
    CHECK(captured.mid(at - frameBytes, frameBytes) !=
          expected.mid((start + target - 1) * frameBytes, frameBytes));
    WARN(c.name << "：跳轉到 " << c.targetMs << " 毫秒（第 " << target
                << " 個樣本框），送出的第一個樣本框誤差 0");
}
