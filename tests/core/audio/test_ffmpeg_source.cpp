// FFmpeg 音源：無損格式逐位元相同、有損格式可以解碼、Unicode 路徑、損毀檔、seek。
// 測試訊號在測試時用 FFmpeg 編碼產生（.localdata\tmp\ 底下），MP3 用 tests/fixtures 的小檔。
#include "audio/ffmpeg_source.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <random>

using auralis::AudioFormat;
using auralis::IAudioSource;
using auralis::core::FfmpegSourceFactory;
using auralis::test::encodeFile;
using auralis::test::makeNoise;

namespace {

QString testRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

AudioFormat pcm(quint32 rate, quint16 bits, bool isFloat = false) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    format.isFloat = isFloat;
    return format;
}

std::unique_ptr<IAudioSource> openSource(const QString& path) {
    FfmpegSourceFactory factory;
    auto source = factory.open(path);
    const QString why =
        source ? QString() : source.error().call + QStringLiteral("：") + source.error().message;
    INFO(why.toStdString());
    REQUIRE(source.hasValue());
    return std::move(source).value();
}

/// 一次讀 chunk 個樣本框直到結尾；中途回傳 Error 時 REQUIRE 失敗。
QByteArray readAll(IAudioSource& source, qint64 chunk = 1000) {
    const int frameBytes = source.format().bytesPerFrame();
    QByteArray all;
    QByteArray buffer(chunk * frameBytes, Qt::Uninitialized);
    for (;;) {
        const auto read = source.read(reinterpret_cast<std::byte*>(buffer.data()), chunk);
        REQUIRE(read.hasValue());
        if (read.value() == 0) {
            return all;
        }
        all.append(buffer.constData(), read.value() * frameBytes);
    }
}

struct LosslessCase {
    const char* codec;
    const char* muxer;
    const char* suffix;
    quint32 rate;
    quint16 bits;
    bool isFloat;
};

} // namespace

TEST_CASE("AC-01-2：無損格式以原始位元深度與取樣率解碼，樣本逐位元相同") {
    const auto c = GENERATE(LosslessCase{"flac", "flac", "flac", 44100, 16, false},
                            LosslessCase{"flac", "flac", "flac", 96000, 24, false},
                            LosslessCase{"flac", "flac", "flac", 192000, 24, false},
                            LosslessCase{"flac", "flac", "flac", 384000, 24, false},
                            LosslessCase{"pcm_s16le", "wav", "wav", 48000, 16, false},
                            LosslessCase{"pcm_s24le", "wav", "wav", 96000, 24, false},
                            LosslessCase{"pcm_s32le", "wav", "wav", 192000, 32, false},
                            LosslessCase{"pcm_f32le", "wav", "wav", 384000, 32, true},
                            LosslessCase{"pcm_s16be", "aiff", "aiff", 44100, 16, false},
                            LosslessCase{"pcm_s24be", "aiff", "aif", 88200, 24, false},
                            LosslessCase{"alac", "ipod", "m4a", 44100, 16, false},
                            LosslessCase{"alac", "ipod", "m4a", 96000, 24, false},
                            LosslessCase{"wavpack", "wv", "wv", 44100, 16, false},
                            LosslessCase{"wavpack", "wv", "wv", 192000, 24, false});
    INFO(c.codec << " " << c.rate << " Hz " << c.bits << " 位元");
    QTemporaryDir dir(testRoot() + QStringLiteral("/source-XXXXXX"));
    REQUIRE(dir.isValid());
    const AudioFormat format = pcm(c.rate, c.bits, c.isFloat);
    const int frames = static_cast<int>(c.rate / 4); // 0.25 秒
    const QByteArray expected = makeNoise(format, frames, c.rate + c.bits);
    const QString path = dir.filePath(QStringLiteral("signal.%1").arg(QLatin1String(c.suffix)));
    REQUIRE(encodeFile(path, c.codec, c.muxer, format, expected).isEmpty());

    auto source = openSource(path);
    CHECK(source->format() == format);
    CHECK(source->lengthFrames() == frames);
    const QByteArray decoded = readAll(*source);
    CHECK(decoded.size() == expected.size());
    CHECK(decoded == expected);
}

TEST_CASE("AC-01-1：有損格式（AAC、Opus、Vorbis、MP3）可以解碼，長度誤差在 0.5 秒內") {
    struct LossyCase {
        const char* codec;
        const char* muxer;
        const char* suffix;
    };
    const auto c = GENERATE(LossyCase{"aac", "ipod", "m4a"}, LossyCase{"aac", "adts", "aac"},
                            LossyCase{"opus", "ogg", "opus"}, LossyCase{"vorbis", "ogg", "ogg"},
                            LossyCase{"mp3", "", "mp3"});
    INFO(c.codec << " ." << c.suffix);
    QTemporaryDir dir(testRoot() + QStringLiteral("/source-XXXXXX"));
    REQUIRE(dir.isValid());
    QString path;
    // 0.5 振幅正弦的 RMS；MP3 小檔是 FFmpeg sine 濾鏡的預設振幅 1/8
    double expectedRms = 0.5 / std::sqrt(2.0);
    quint32 rate = 48000;
    qint64 frames = rate; // 1 秒
    if (QLatin1String(c.codec) == QLatin1String("mp3")) {
        // FFmpeg 函式庫沒有 MP3 編碼器（LAME 不在 LGPL 建置裡），用預先產生的小檔
        path = QStringLiteral(AURALIS_FIXTURES "/audio/sine-440hz-1s.mp3");
        rate = 44100;
        frames = rate;
        expectedRms = 0.125 / std::sqrt(2.0);
    } else {
        // 440 Hz 正弦，有損編碼器才有東西可以保留
        AudioFormat format = pcm(rate, 32, true);
        QByteArray signal(frames * format.bytesPerFrame(), Qt::Uninitialized);
        auto* samples = reinterpret_cast<float*>(signal.data());
        for (qint64 i = 0; i < frames; ++i) {
            samples[2 * i] = samples[2 * i + 1] =
                0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
        }
        path = dir.filePath(QStringLiteral("signal.%1").arg(QLatin1String(c.suffix)));
        REQUIRE(encodeFile(path, c.codec, c.muxer, format, signal).isEmpty());
    }

    auto source = openSource(path);
    CHECK(source->format().rate == rate);
    CHECK(source->format().channels == 2);
    CHECK(source->format().isFloat);
    const QByteArray decoded = readAll(*source);
    const qint64 decodedFrames = decoded.size() / source->format().bytesPerFrame();
    CHECK(std::abs(decodedFrames - frames) <= rate / 2);
    CHECK(std::abs(source->lengthFrames() - frames) <= rate / 2);
    // 不是靜音：解出來的能量和原訊號同一個量級
    const auto* samples = reinterpret_cast<const float*>(decoded.constData());
    double energy = 0;
    for (qint64 i = 0; i < decodedFrames * 2; ++i) {
        energy += samples[i] * samples[i];
    }
    const double rms = std::sqrt(energy / static_cast<double>(decodedFrames * 2));
    CHECK(rms > expectedRms / 2);
    CHECK(rms < expectedRms * 1.5);
}

TEST_CASE("AC-01-4：路徑含中日文、全形符號與罕見 Unicode 字元時正常開啟與解碼") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/source-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("Rё∀L/ＮieR：Automata/遺されたもの 𝄞"));
    REQUIRE(QDir().mkpath(folder));
    const QString path = folder + QStringLiteral("/曲目 ０１ ♪.flac");
    const AudioFormat format = pcm(44100, 16);
    const QByteArray expected = makeNoise(format, 4410, 7);
    REQUIRE(encodeFile(path, "flac", "flac", format, expected).isEmpty());
    REQUIRE(QFileInfo::exists(path));

    auto source = openSource(path);
    CHECK(readAll(*source) == expected);
}

TEST_CASE("AC-01-3：損毀或不支援的檔案回傳錯誤，不會當掉") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/source-XXXXXX"));
    REQUIRE(dir.isValid());
    FfmpegSourceFactory factory;
    const auto write = [&](const QString& name, const QByteArray& bytes) {
        QFile file(dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        return file.fileName();
    };
    std::mt19937 random(1);
    QByteArray garbage(64 * 1024, Qt::Uninitialized);
    for (auto& byte : garbage) {
        byte = static_cast<char>(random());
    }

    SECTION("不存在的檔案") {
        CHECK_FALSE(factory.open(dir.filePath(QStringLiteral("沒有這個檔.flac"))).hasValue());
    }
    SECTION("0 位元組") {
        CHECK_FALSE(factory.open(write(QStringLiteral("empty.flac"), {})).hasValue());
    }
    SECTION("亂碼") {
        CHECK_FALSE(factory.open(write(QStringLiteral("garbage.flac"), garbage)).hasValue());
    }
    SECTION("文字檔改副檔名") {
        CHECK_FALSE(
            factory.open(write(QStringLiteral("text.mp3"), QByteArray("not audio\n").repeated(50)))
                .hasValue());
    }

    const AudioFormat format = pcm(44100, 16);
    const QByteArray signal = makeNoise(format, 44100, 3);
    const QString good = dir.filePath(QStringLiteral("good.flac"));
    REQUIRE(encodeFile(good, "flac", "flac", format, signal).isEmpty());
    QFile file(good);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();

    SECTION("只剩前半段：解到斷掉的地方為止，不當掉") {
        auto source =
            factory.open(write(QStringLiteral("half.flac"), bytes.left(bytes.size() / 2)));
        REQUIRE(source.hasValue());
        QByteArray buffer(4096 * format.bytesPerFrame(), Qt::Uninitialized);
        qint64 total = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto read =
                source.value()->read(reinterpret_cast<std::byte*>(buffer.data()), 4096);
            if (!read || read.value() == 0) {
                break;
            }
            total += read.value();
        }
        CHECK(total > 0);
        CHECK(total < 44100);
    }
    SECTION("中間被亂碼蓋掉：繼續解或回傳錯誤，不當掉") {
        QByteArray broken = bytes;
        broken.replace(broken.size() / 3, 8192, garbage.left(8192));
        auto source = factory.open(write(QStringLiteral("broken.flac"), broken));
        if (source) {
            QByteArray buffer(4096 * format.bytesPerFrame(), Qt::Uninitialized);
            for (int i = 0; i < 1000; ++i) {
                const auto read =
                    source.value()->read(reinterpret_cast<std::byte*>(buffer.data()), 4096);
                if (!read || read.value() == 0) {
                    break;
                }
            }
        }
        SUCCEED("沒有當掉");
    }
}

TEST_CASE("音源：seek 之後的樣本與從頭解碼到那裡相同") {
    struct SeekCase {
        const char* codec;
        const char* muxer;
        const char* suffix;
    };
    const auto c = GENERATE(SeekCase{"flac", "flac", "flac"}, SeekCase{"pcm_s24le", "wav", "wav"},
                            SeekCase{"wavpack", "wv", "wv"});
    INFO(c.codec);
    QTemporaryDir dir(testRoot() + QStringLiteral("/source-XXXXXX"));
    REQUIRE(dir.isValid());
    const AudioFormat format = pcm(96000, 24);
    const QByteArray expected = makeNoise(format, 96000, 11); // 1 秒
    const QString path = dir.filePath(QStringLiteral("seek.%1").arg(QLatin1String(c.suffix)));
    REQUIRE(encodeFile(path, c.codec, c.muxer, format, expected).isEmpty());

    auto source = openSource(path);
    const int frameBytes = format.bytesPerFrame();
    for (const qint64 target : {qint64{70001}, qint64{12345}, qint64{0}, qint64{95999}}) {
        INFO("seek 到 " << target);
        const auto sought = source->seek(target);
        REQUIRE(sought.hasValue());
        const QByteArray rest = readAll(*source, 777);
        CHECK(rest == expected.mid(target * frameBytes));
    }
}
