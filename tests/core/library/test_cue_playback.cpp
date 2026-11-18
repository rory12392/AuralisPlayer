// CUE 區段播放（AC-07-2）：起訖與 .cue 的時間一致（≤ 1/75 秒）、相鄰軌連續播放沒有空白。
// 用 Capture 後端逐位元比對：送到裝置的資料要剛好是整軌檔解碼資料的對應區段。
#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "cue_fixtures.h"
#include "library_fixtures.h"

#include "audio/processor_chain_impl.h"
#include "audio/source_registry_impl.h"
#include "library/cue_tracks.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QFileInfo>

#include <chrono>
#include <cmath>
#include <deque>
#include <thread>

using auralis::AudioFormat;
using auralis::SourceRegistry;
using auralis::TrackRef;
using namespace auralis::core;
using namespace auralis::test;

namespace {

/// 依序把曲目身分交給 openTrack 開好，當成下一首（PlaybackService 的佇列之後也是這樣做）。
class RefProvider final : public NextTrackProvider {
public:
    RefProvider(const SourceRegistry& sources, std::deque<TrackRef> refs)
        : m_sources(sources), m_refs(std::move(refs)) {}
    std::optional<EngineTrack> nextTrack() override {
        if (m_refs.empty()) {
            return std::nullopt;
        }
        const TrackRef ref = m_refs.front();
        m_refs.pop_front();
        auto track = openTrack(ref, m_sources, ref.subsong);
        if (!track) {
            return std::nullopt;
        }
        return std::move(track).value();
    }

private:
    const SourceRegistry& m_sources;
    std::deque<TrackRef> m_refs;
};

struct Album {
    AudioFormat format;
    QByteArray pcm; ///< 整軌檔的解碼資料
    QString cuePath;
};

/// 在測試資料夾產生 5 秒的整軌 FLAC（指定格式的雜訊）與樣本 .cue（三軌）。
/// name 每個測試案例不同：CTest 平行跑時各自一個資料夾，不會互相清掉。
Album makeAlbum(const QString& name, quint32 rate, quint16 bits) {
    Album album;
    album.format.rate = rate;
    album.format.bits = bits;
    album.format.container = bits == 16 ? 16 : 32;
    album.format.channels = 2;
    const QString dir = freshTestDir(QStringLiteral("t-11-%1-%2-%3").arg(name).arg(rate).arg(bits));
    album.pcm = makeNoise(album.format, static_cast<int>(rate) * kSampleAudioSeconds, rate + bits);
    const QString why =
        encodeFile(dir + QStringLiteral("\\album.flac"), "flac", "flac", album.format, album.pcm);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    album.cuePath = dir + QStringLiteral("\\album.cue");
    writeBytes(album.cuePath, cueFixture(QStringLiteral("utf8-bom.cue")));
    return album;
}

bool waitUntilFinished(PlaybackEngine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return engine.isFinished();
}

} // namespace

TEST_CASE("AC-07-2：CUE 曲目的起訖位置與 .cue 的時間一致（誤差 ≤ 1/75 秒）") {
    const auto [rate, bits] =
        GENERATE(std::pair<quint32, quint16>{44100, 16}, std::pair<quint32, quint16>{48000, 24},
                 std::pair<quint32, quint16>{96000, 24});
    CAPTURE(rate, bits);
    const Album album = makeAlbum(QStringLiteral("bounds"), rate, bits);
    SourceRegistryImpl sources;
    const qint64 total = static_cast<qint64>(rate) * kSampleAudioSeconds;
    for (int i = 0; i < 3; ++i) {
        CAPTURE(i);
        auto track = openTrack({album.cuePath, static_cast<quint32>(i + 1)}, sources, 7);
        REQUIRE(track.hasValue());
        const double expectedStart = kSampleTrackStart[i] * static_cast<double>(rate) / 75.0;
        CHECK(std::abs(track.value().startFrame - expectedStart) <= rate / 75.0);
        CHECK(track.value().startFrame == static_cast<qint64>(kSampleTrackStart[i]) * rate / 75);
        if (i < 2) {
            CHECK(track.value().endFrame ==
                  static_cast<qint64>(kSampleTrackStart[i + 1]) * rate / 75);
        } else {
            CHECK(track.value().endFrame == -1); // 到整軌檔結尾
            CHECK(track.value().source->lengthFrames() == total);
        }
        CHECK(track.value().token == 7);
    }

    // 單獨播第 2 軌：裝置收到的剛好是整軌檔的那一段，之後全是靜音
    const qint64 frameBytes = album.format.bytesPerFrame();
    const qint64 start = static_cast<qint64>(kSampleTrackStart[1]) * rate / 75;
    const qint64 end = static_cast<qint64>(kSampleTrackStart[2]) * rate / 75;
    auto second = openTrack({album.cuePath, 2}, sources, 2);
    REQUIRE(second.hasValue());
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(std::move(second).value(), capture).hasValue());
    REQUIRE(waitUntilFinished(engine));
    const auto position = engine.position();
    engine.stop();
    CHECK(startsWithThenSilence(capture.captured(),
                                album.pcm.mid(start * frameBytes, (end - start) * frameBytes)));
    CHECK(position.token == 2);
    CHECK(position.frames == end - start);
}

TEST_CASE("AC-07-2：CUE 曲目連續播放時沒有空白，三軌接起來等於整軌檔") {
    const auto [rate, bits] =
        GENERATE(std::pair<quint32, quint16>{44100, 16}, std::pair<quint32, quint16>{96000, 24});
    CAPTURE(rate, bits);
    const Album album = makeAlbum(QStringLiteral("gapless"), rate, bits);
    SourceRegistryImpl sources;
    RefProvider provider(sources, {{album.cuePath, 2}, {album.cuePath, 3}});
    auto first = openTrack({album.cuePath, 1}, sources, 1);
    REQUIRE(first.hasValue());

    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(std::move(first).value(), capture).hasValue());
    REQUIRE(waitUntilFinished(engine));
    const auto position = engine.position();
    engine.stop();

    CHECK_FALSE(engine.decodeError().has_value());
    CHECK(engine.underruns() == 0);
    CHECK(capture.sessions().size() == 1); // 同一個檔案的區段不重開裝置
    CHECK(startsWithThenSilence(capture.captured(), album.pcm));
    CHECK(position.token == 3);
    CHECK(position.frames == static_cast<qint64>(rate) * kSampleAudioSeconds -
                                 static_cast<qint64>(kSampleTrackStart[2]) * rate / 75);
}

TEST_CASE("openTrack：整檔從頭播到尾；.cue 沒有那一軌或找不到音訊檔時回傳錯誤") {
    const Album album = makeAlbum(QStringLiteral("open"), 44100, 16);
    SourceRegistryImpl sources;
    const QString flac =
        QString(album.cuePath).replace(QStringLiteral(".cue"), QStringLiteral(".flac"));
    auto whole = openTrack({flac, 0}, sources, 5);
    REQUIRE(whole.hasValue());
    CHECK(whole.value().startFrame == 0);
    CHECK(whole.value().endFrame == -1);
    CHECK(whole.value().token == 5);

    CHECK_FALSE(openTrack({album.cuePath, 9}, sources).hasValue());
    const QString orphan = QFileInfo(album.cuePath).absolutePath() + QStringLiteral("/orphan.cue");
    writeBytes(orphan, QByteArrayLiteral("FILE \"gone.flac\" WAVE\n TRACK 01 AUDIO\n"
                                         " INDEX 01 00:00:00\n"));
    const auto missing = openTrack({orphan, 1}, sources);
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message.contains(QStringLiteral("gone.flac")));
}
