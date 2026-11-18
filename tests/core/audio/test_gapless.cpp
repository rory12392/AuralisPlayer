// 無縫播放（US-05）：同格式接續（AC-05-1）與換格式的靜音切換（AC-05-3），用 Capture
// 後端做位元比對。
#include "audio/ffmpeg_source.h"
#include "engine_helpers.h"
#include "fake_wasapi.h"
#include "signal_writer.h"

#include "audio/wasapi_backend.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>

#include <cmath>
#include <deque>
#include <numbers>

using auralis::AudioFormat;
using auralis::OutputErrorKind;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::FfmpegSourceFactory;
using auralis::core::NextTrackProvider;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::WasapiBackend;
using auralis::core::WasapiMode;
using auralis::test::encodeFile;
using auralis::test::FakeDevice;
using auralis::test::FakeEndpoints;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using auralis::test::supportsOnly;

namespace {

/// 換格式時頭尾各送的靜音下限（毫秒），和引擎的設定相同。
constexpr int kSilenceMs = 50;

AudioFormat pcm(quint32 rate, quint16 bits) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    return format;
}

/// 依序交出事先準備好的曲目；記錄被問了幾次。
class ListProvider final : public NextTrackProvider {
public:
    void add(std::unique_ptr<auralis::IAudioSource> source, quint64 token, qint64 start = 0,
             qint64 end = -1) {
        m_tracks.push_back(EngineTrack{std::move(source), start, end, token});
    }
    std::optional<EngineTrack> nextTrack() override {
        ++calls;
        if (m_tracks.empty()) {
            return std::nullopt;
        }
        EngineTrack track = std::move(m_tracks.front());
        m_tracks.pop_front();
        return track;
    }
    std::atomic<int> calls{0};

private:
    std::deque<EngineTrack> m_tracks;
};

/// 等到播完（不停止，讓測試還能讀 position()）。
bool waitUntilFinished(PlaybackEngine& engine, int timeoutMs = 30000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return engine.isFinished();
}

/// 產生頭尾淡入淡出（從 0 開始、在 0 結束）的正弦，排列方式和音源交出的相同。
QByteArray makeSine(const AudioFormat& format, int frames, double hz) {
    QByteArray data(static_cast<qsizetype>(frames) * format.bytesPerFrame(), 0);
    const int fade = frames / 8;
    for (int i = 0; i < frames; ++i) {
        double gain = 0.5;
        if (i < fade) {
            gain *= static_cast<double>(i) / fade;
        } else if (i >= frames - fade) {
            gain *= static_cast<double>(frames - 1 - i) / fade;
        }
        const double value = gain * std::sin(2.0 * std::numbers::pi * hz * i / format.rate);
        for (int c = 0; c < format.channels; ++c) {
            char* out = data.data() +
                        (static_cast<qsizetype>(i) * format.channels + c) * (format.container / 8);
            if (format.container == 16) {
                const auto s = static_cast<qint16>(std::lround(value * 32767.0));
                std::memcpy(out, &s, sizeof(s));
            } else {
                const auto s = static_cast<qint32>(std::lround(value * 8388607.0)) << 8;
                std::memcpy(out, &s, sizeof(s));
            }
        }
    }
    return data;
}

/// 相鄰樣本框之間最大的跳變（滿刻度 = 1.0）；爆音會讓它變大。
double maxJump(const AudioFormat& format, const QByteArray& data) {
    const int sampleBytes = format.container / 8;
    const qsizetype frames = data.size() / format.bytesPerFrame();
    const double scale = format.container == 16 ? 32768.0 : 2147483648.0;
    double jump = 0.0;
    for (int c = 0; c < format.channels; ++c) {
        double previous = 0.0;
        for (qsizetype i = 0; i < frames; ++i) {
            const char* in = data.constData() + (i * format.channels + c) * sampleBytes;
            double value = 0.0;
            if (sampleBytes == 2) {
                qint16 s;
                std::memcpy(&s, in, sizeof(s));
                value = s / scale;
            } else {
                qint32 s;
                std::memcpy(&s, in, sizeof(s));
                value = s / scale;
            }
            jump = std::max(jump, std::abs(value - previous));
            previous = value;
        }
    }
    return jump;
}

bool allZero(const QByteArray& data) {
    return std::all_of(data.begin(), data.end(), [](char b) { return b == 0; });
}

/// 產生雜訊並編成 FLAC，回傳檔案路徑。
QString makeFlac(const AudioFormat& format, int frames, quint32 seed, QByteArray& data) {
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t07-gapless");
    QDir().mkpath(dir);
    const QString path =
        dir + QStringLiteral("/noise-%1-%2-%3.flac").arg(format.rate).arg(format.bits).arg(seed);
    data = makeNoise(format, frames, seed);
    const QString why = encodeFile(path, "flac", "flac", format, data);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    return path;
}

} // namespace

TEST_CASE("AC-05-1：同格式兩首 FLAC 相接，送到裝置的資料等於兩首解碼資料直接相接") {
    struct Case {
        quint32 rate;
        quint16 bits;
        int framesA;
        int framesB;
    };
    // 第一組比 5 秒長（剩 5 秒時預開）；第二組很短（第一個區塊就預開）
    const auto c =
        GENERATE(Case{44100, 16, 44100 * 6 + 123, 44100 * 2 + 77}, Case{96000, 24, 9000, 13001});
    const AudioFormat format = pcm(c.rate, c.bits);
    CAPTURE(c.rate, c.bits);
    QByteArray a;
    QByteArray b;
    const QString pathA = makeFlac(format, c.framesA, 11, a);
    const QString pathB = makeFlac(format, c.framesB, 12, b);
    FfmpegSourceFactory sources;
    auto sourceA = sources.open(pathA);
    auto sourceB = sources.open(pathB);
    REQUIRE(sourceA.hasValue());
    REQUIRE(sourceB.hasValue());

    ListProvider provider;
    provider.add(std::move(sourceB).value(), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    CaptureBackend::Options options = fastCapture();
    options.maxCaptureSeconds = 12;
    CaptureBackend capture(options);
    REQUIRE(engine.play(EngineTrack{std::move(sourceA).value(), 0, -1, 1}, capture).hasValue());
    REQUIRE(waitUntilFinished(engine));
    const PlaybackEngine::Position position = engine.position();
    engine.stop();

    CHECK_FALSE(engine.decodeError().has_value());
    CHECK(engine.underruns() == 0);
    CHECK(capture.sessions().size() == 1);
    CHECK(startsWithThenSilence(capture.captured(), a + b));
    CHECK(position.token == 2);
    CHECK(position.frames == c.framesB);
    CHECK(provider.calls == 2);
}

TEST_CASE("AC-05-1：連續幾首很短的曲目與相鄰區段（CUE）相接，中間沒有插入靜音") {
    const AudioFormat format = pcm(48000, 24);
    const QByteArray whole = makeNoise(format, 30000, 21);
    const QByteArray tiny1 = makeNoise(format, 3000, 22);
    const QByteArray tiny2 = makeNoise(format, 1500, 23);
    // 第一首是 whole 的前段、第二首是後段（同一個檔案的相鄰 CUE 區段），之後接兩首很短的
    ListProvider provider;
    provider.add(std::make_unique<MemorySource>(format, whole), 2, 12345, -1);
    provider.add(std::make_unique<MemorySource>(format, tiny1), 3);
    provider.add(std::make_unique<MemorySource>(format, tiny2), 4);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    EngineTrack first{std::make_unique<MemorySource>(format, whole), 0, 12345, 1};
    REQUIRE(engine.play(std::move(first), capture).hasValue());
    REQUIRE(waitUntilFinished(engine));
    const PlaybackEngine::Position position = engine.position();
    engine.stop();

    CHECK(engine.underruns() == 0);
    CHECK(startsWithThenSilence(capture.captured(), whole + tiny1 + tiny2));
    CHECK(position.token == 4);
    CHECK(position.frames == 1500);
}

TEST_CASE("AC-05-3：前後兩首格式不同時，排空、送靜音、重開裝置，切換處沒有爆音") {
    const AudioFormat formatA = pcm(44100, 16);
    const AudioFormat formatB = pcm(96000, 24);
    const QByteArray a = makeSine(formatA, 17640, 440.0);
    const QByteArray b = makeSine(formatB, 38400, 1000.0);
    ListProvider provider;
    provider.add(std::make_unique<MemorySource>(formatB, b), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(EngineTrack{std::make_unique<MemorySource>(formatA, a), 0, -1, 1}, capture)
                .hasValue());
    REQUIRE(waitUntilFinished(engine));
    const PlaybackEngine::Position position = engine.position();
    const PlaybackEngine::Status status = engine.status();
    engine.stop();

    CHECK_FALSE(engine.decodeError().has_value());
    CHECK(engine.underruns() == 0);
    CHECK(position.token == 2);
    CHECK(position.frames == 38400);
    CHECK(status.sourceFormat == formatB);
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 2);

    // 第一段：前一首完整送出，之後只有數位靜音，而且裝置在送靜音時才被關掉
    const auto& first = sessions[0];
    CHECK(first.format == formatA);
    REQUIRE(first.data.size() > a.size());
    CHECK(first.data.left(a.size()) == a);
    const QByteArray tail = first.data.mid(a.size());
    CHECK(allZero(tail));
    CHECK(tail.size() >= formatA.rate * kSilenceMs / 1000 * formatA.bytesPerFrame());
    CHECK(maxJump(formatA, first.data) <= maxJump(formatA, a));

    // 第二段：重開後先是靜音，再接完整的下一首
    const auto& second = sessions[1];
    CHECK(second.format == formatB);
    const qsizetype at = second.data.indexOf(b);
    REQUIRE(at >= 0);
    CHECK(allZero(second.data.left(at)));
    CHECK(at >= formatB.rate * kSilenceMs / 1000 * formatB.bytesPerFrame());
    CHECK(allZero(second.data.mid(at + b.size())));
    CHECK(maxJump(formatB, second.data) <= maxJump(formatB, b));
}

TEST_CASE("AC-05-3：換格式時裝置不支援新格式，回報開不了的那首與錯誤種類") {
    auto device = std::make_shared<FakeDevice>();
    device->supports = supportsOnly({{44100, 16, 16}});
    WasapiBackend backend(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device),
                          device->id);
    const AudioFormat formatA = pcm(44100, 16);
    const AudioFormat formatB = pcm(96000, 24);
    ListProvider provider;
    provider.add(std::make_unique<MemorySource>(formatB, makeNoise(formatB, 4800, 31)), 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    EngineTrack first{std::make_unique<MemorySource>(formatA, makeSine(formatA, 4410, 440.0)), 0,
                      -1, 1};
    REQUIRE(engine.play(std::move(first), backend).hasValue());
    REQUIRE(waitUntilFinished(engine));
    CHECK(engine.decodeError().has_value());
    CHECK(engine.position().token == 2);
    CHECK(backend.lastErrorKind() == OutputErrorKind::FormatNotSupported);
    engine.stop();
}
