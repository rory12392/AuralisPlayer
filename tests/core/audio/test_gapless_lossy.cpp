// AC-05-2：MP3（LAME 標頭）與 AAC（MP4 的 edit list）依 gapless 資訊去掉編碼器延遲與結尾補零。
// 證據：解出來的樣本框數剛好等於原始長度，而且正弦的相位和原訊號一致（沒有時間位移）。
// MP3 用 tests/fixtures 的小檔（LAME 編碼、1 秒 440 Hz、從相位 0 開始）；AAC 在測試時用 FFmpeg
// 編碼。
#include "audio/ffmpeg_source.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>

#include <cmath>
#include <numbers>
#include <vector>

using auralis::AudioFormat;
using auralis::IAudioSource;
using auralis::core::FfmpegSourceFactory;
using auralis::test::encodeFile;

namespace {

/// 頭尾不算進相位估計的樣本數（編碼器在邊緣的暫態）。
constexpr qint64 kEdge = 2048;

/// 解到結尾，回傳第一個聲道（有損來源是 32 位元浮點）。
std::vector<double> decodeLeft(IAudioSource& source) {
    const AudioFormat format = source.format();
    REQUIRE(format.isFloat);
    std::vector<float> chunk(static_cast<std::size_t>(4096) * format.channels);
    std::vector<double> left;
    for (;;) {
        auto read = source.read(reinterpret_cast<std::byte*>(chunk.data()), 4096);
        REQUIRE(read.hasValue());
        if (read.value() == 0) {
            return left;
        }
        for (qint64 i = 0; i < read.value(); ++i) {
            left.push_back(chunk[static_cast<std::size_t>(i * format.channels)]);
        }
    }
}

/// 以最小平方法擬合 sin(2π·hz·n/rate) 的相位，換算成樣本位移（落在 ±半個週期內）。
/// 原訊號從相位 0 開始，所以沒有位移時接近 0；多出編碼器延遲時會偏掉。
double phaseOffset(const std::vector<double>& decoded, double hz, quint32 rate) {
    double s = 0.0;
    double c = 0.0;
    const auto n = static_cast<qint64>(decoded.size());
    for (qint64 i = kEdge; i < n - kEdge; ++i) {
        const double w = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate;
        s += decoded[static_cast<std::size_t>(i)] * std::sin(w);
        c += decoded[static_cast<std::size_t>(i)] * std::cos(w);
    }
    return std::atan2(c, s) / (2.0 * std::numbers::pi * hz / rate);
}

/// 前面多出 delay 個樣本時的樣子（沒有處理 gapless 資訊的解碼結果），證明測試分得出來。
std::vector<double> delayed(const std::vector<double>& decoded, std::size_t delay) {
    std::vector<double> out(delay, 0.0);
    out.insert(out.end(), decoded.begin(), decoded.end());
    return out;
}

} // namespace

TEST_CASE("AC-05-2：MP3 依 LAME 標頭去掉編碼器延遲與補零，長度剛好、沒有位移") {
    FfmpegSourceFactory sources;
    auto source = sources.open(QStringLiteral(AURALIS_FIXTURES "/audio/sine-440hz-1s.mp3"));
    REQUIRE(source.hasValue());
    const AudioFormat format = source.value()->format();
    REQUIRE(format.rate == 44100);
    const std::vector<double> left = decodeLeft(*source.value());
    // 沒有去延遲時會多出 LAME 的 576＋529 個樣本，結尾還會多補零到 1152 的倍數
    CHECK(left.size() == 44100);
    CHECK(source.value()->lengthFrames() == 44100);
    const double aligned = phaseOffset(left, 440.0, format.rate);
    const double shifted = phaseOffset(delayed(left, 1105), 440.0, format.rate);
    CAPTURE(aligned, shifted);
    CHECK(std::abs(aligned) < 0.5);
    CHECK(std::abs(shifted) > 1.0);
}

TEST_CASE("AC-05-2：AAC（.m4a）依 edit list 去掉編碼器延遲（priming）與補零，長度剛好、沒有位移") {
    const quint32 rate = 44100;
    // 不是 1024 的倍數，結尾一定有補零要去掉
    const qint64 frames = rate + 333;
    AudioFormat format;
    format.rate = rate;
    format.bits = 32;
    format.container = 32;
    format.channels = 2;
    format.isFloat = true;
    QByteArray signal(frames * format.bytesPerFrame(), Qt::Uninitialized);
    auto* samples = reinterpret_cast<float*>(signal.data());
    for (qint64 i = 0; i < frames; ++i) {
        samples[2 * i] = samples[2 * i + 1] =
            0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / rate));
    }
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t07-gapless");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/sine-1khz.m4a");
    const QString why = encodeFile(path, "aac", "ipod", format, signal);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());

    FfmpegSourceFactory sources;
    auto source = sources.open(path);
    REQUIRE(source.hasValue());
    const std::vector<double> left = decodeLeft(*source.value());
    // 沒有去延遲時會多出 1024 個 priming 樣本，結尾補到 1024 的倍數
    CHECK(static_cast<qint64>(left.size()) == frames);
    CHECK(source.value()->lengthFrames() == frames);
    const double aligned = phaseOffset(left, 1000.0, rate);
    const double shifted = phaseOffset(delayed(left, 1024), 1000.0, rate);
    CAPTURE(aligned, shifted);
    CHECK(std::abs(aligned) < 0.5);
    CHECK(std::abs(shifted) > 1.0);
}
