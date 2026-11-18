// 依曲目身分開音源（SourceRegistry::openTrack，給模組用，例：R128 計算 CUE 曲目）：
// 整檔是整個檔案；CUE 曲目回傳已切好範圍的音源，讀出來剛好是整軌檔解碼資料的那一段。
#include "../audio/signal_writer.h"
#include "cue_fixtures.h"
#include "library_fixtures.h"

#include "audio/source_registry_impl.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QFileInfo>

#include <cstddef>
#include <vector>

using auralis::AudioFormat;
using auralis::IAudioSource;
using auralis::TrackRef;
using namespace auralis::core;
using namespace auralis::test;

namespace {

struct Album {
    AudioFormat format;
    QByteArray pcm; ///< 整軌檔的解碼資料
    QString flacPath;
    QString cuePath;
};

/// 5 秒的整軌 FLAC（雜訊）加樣本 .cue（三軌）。name 每個測試案例不同，CTest 平行跑時不互相清掉。
Album makeAlbum(const QString& name, quint32 rate, quint16 bits) {
    Album album;
    album.format.rate = rate;
    album.format.bits = bits;
    album.format.container = bits == 16 ? 16 : 32;
    album.format.channels = 2;
    const QString dir =
        freshTestDir(QStringLiteral("fix-open-track-%1-%2-%3").arg(name).arg(rate).arg(bits));
    album.pcm = makeNoise(album.format, static_cast<int>(rate) * kSampleAudioSeconds, rate + bits);
    album.flacPath = dir + QStringLiteral("\\album.flac");
    const QString why = encodeFile(album.flacPath, "flac", "flac", album.format, album.pcm);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    album.cuePath = dir + QStringLiteral("\\album.cue");
    writeBytes(album.cuePath, cueFixture(QStringLiteral("utf8-bom.cue")));
    return album;
}

/// 從目前位置讀到音源回傳 0 為止。
QByteArray readToEnd(IAudioSource& source) {
    const qint64 frameBytes = source.format().bytesPerFrame();
    std::vector<std::byte> buffer(static_cast<std::size_t>(1000 * frameBytes));
    QByteArray all;
    for (;;) {
        auto read = source.read(buffer.data(), 1000);
        REQUIRE(read.hasValue());
        if (read.value() == 0) {
            return all;
        }
        all.append(reinterpret_cast<const char*>(buffer.data()), read.value() * frameBytes);
    }
}

} // namespace

TEST_CASE("AC-07-2：依曲目身分開的 CUE 音源已切成這一軌的範圍，讀到的是整軌檔的那一段") {
    const auto [rate, bits] =
        GENERATE(std::pair<quint32, quint16>{44100, 16}, std::pair<quint32, quint16>{96000, 24});
    CAPTURE(rate, bits);
    const Album album = makeAlbum(QStringLiteral("range"), rate, bits);
    SourceRegistryImpl sources;
    const qint64 frameBytes = album.format.bytesPerFrame();
    const qint64 total = static_cast<qint64>(rate) * kSampleAudioSeconds;
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        const qint64 start = static_cast<qint64>(kSampleTrackStart[i]) * rate / 75;
        const qint64 end =
            i < 2 ? static_cast<qint64>(kSampleTrackStart[i + 1]) * rate / 75 : total;
        auto source = sources.openTrack({album.cuePath, static_cast<quint32>(i + 1)});
        REQUIRE(source.hasValue());
        CHECK(source.value()->format() == album.format);
        CHECK(source.value()->lengthFrames() == end - start);
        CHECK(readToEnd(*source.value()) ==
              album.pcm.mid(start * frameBytes, (end - start) * frameBytes));
    }
}

TEST_CASE("AC-07-2：CUE 音源的 seek 從這一軌的起點算，讀到這一軌結尾就停") {
    const Album album = makeAlbum(QStringLiteral("seek"), 44100, 16);
    SourceRegistryImpl sources;
    const qint64 frameBytes = album.format.bytesPerFrame();
    const qint64 start = static_cast<qint64>(kSampleTrackStart[1]) * 44100 / 75;
    const qint64 end = static_cast<qint64>(kSampleTrackStart[2]) * 44100 / 75;
    auto source = sources.openTrack({album.cuePath, 2});
    REQUIRE(source.hasValue());
    const auto sought = source.value()->seek(1000);
    REQUIRE(sought.hasValue());
    CHECK(sought.value() == 1000);
    CHECK(readToEnd(*source.value()) ==
          album.pcm.mid((start + 1000) * frameBytes, (end - start - 1000) * frameBytes));
    std::vector<std::byte> buffer(static_cast<std::size_t>(frameBytes * 10));
    const auto after = source.value()->read(buffer.data(), 10);
    REQUIRE(after.hasValue());
    CHECK(after.value() == 0);
    CHECK_FALSE(source.value()->seek(-1).hasValue());
}

TEST_CASE("依曲目身分開音源：整檔是整個檔案；沒有那一軌或找不到音訊檔時回傳錯誤") {
    const Album album = makeAlbum(QStringLiteral("whole"), 44100, 16);
    SourceRegistryImpl sources;
    auto whole = sources.openTrack({album.flacPath, 0});
    REQUIRE(whole.hasValue());
    CHECK(whole.value()->lengthFrames() == 44100 * kSampleAudioSeconds);
    CHECK(readToEnd(*whole.value()) == album.pcm);

    CHECK_FALSE(sources.openTrack({album.cuePath, 9}).hasValue());
    const QString orphan = QFileInfo(album.cuePath).absolutePath() + QStringLiteral("/orphan.cue");
    writeBytes(orphan, QByteArrayLiteral("FILE \"gone.flac\" WAVE\n TRACK 01 AUDIO\n"
                                         " INDEX 01 00:00:00\n"));
    const auto missing = sources.openTrack({orphan, 1});
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message.contains(QStringLiteral("gone.flac")));
}
