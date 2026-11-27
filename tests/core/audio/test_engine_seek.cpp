// 播放引擎的跳轉（不重開裝置）與撤回已預開的下一首（佇列改變時，AC-11-2），用 Capture 後端比對。
#include "engine_helpers.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using auralis::AudioFormat;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::NextTrackProvider;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::cd;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using auralis::test::waitFinished;

namespace {

std::unique_ptr<MemorySource> source(const QByteArray& data) {
    return std::make_unique<MemorySource>(cd(), data);
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 20000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return condition();
}

/// 交出的曲目由測試決定；記下每次被問時引擎給的 token。
class SwitchingProvider final : public NextTrackProvider {
public:
    std::optional<EngineTrack> nextTrack() override { return std::nullopt; }
    std::optional<EngineTrack> nextTrackAfter(quint64 token) override {
        const std::lock_guard lock(mutex);
        askedAfter.push_back(token);
        if (!data.has_value()) {
            return std::nullopt;
        }
        return EngineTrack{source(*data), 0, -1, nextToken};
    }
    void offer(std::optional<QByteArray> next, quint64 token) {
        const std::lock_guard lock(mutex);
        data = std::move(next);
        nextToken = token;
    }
    std::vector<quint64> asked() {
        const std::lock_guard lock(mutex);
        return askedAfter;
    }

private:
    std::mutex mutex;
    std::optional<QByteArray> data;
    quint64 nextToken = 0;
    std::vector<quint64> askedAfter;
};

} // namespace

TEST_CASE("AC-11-2：跳轉不重開裝置，之後送出的是新位置的資料，位置從新位置算") {
    const QByteArray track = makeNoise(cd(), 44100 * 4, 31);
    const int frameBytes = cd().bytesPerFrame();
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend::Options options = fastCapture();
    options.speed = 2.0;
    CaptureBackend backend(options);
    REQUIRE(engine.play(EngineTrack{source(track), 0, -1, 9}, backend));
    REQUIRE(waitUntil([&] { return engine.position().frames > 4410; }));

    const qint64 target = 44100 * 3;
    const qint64 heard = engine.position().frames;
    REQUIRE(engine.seek(target));
    REQUIRE(waitUntil([&] { return engine.position().frames >= target; }));
    CHECK(engine.position().token == 9);
    REQUIRE(waitFinished(engine));

    REQUIRE(backend.sessions().size() == 1); // 沒有重開裝置
    const QByteArray captured = backend.captured();
    // 跳轉前送出的是曲目開頭的一段，跳轉時丟掉緩衝、補一小段靜音，之後是新位置到結尾
    const QByteArray tail = track.mid(target * frameBytes);
    const qsizetype at = captured.indexOf(tail.left(4096));
    REQUIRE(at > 0);
    CHECK(startsWithThenSilence(captured.mid(at), tail));
    const QByteArray before = captured.left(at);
    qsizetype played = before.size();
    while (played > 0 && before[played - 1] == 0) {
        --played;
    }
    CHECK(track.startsWith(before.left(played)));
    // 緩衝裡舊位置的資料被丟掉：跳轉之後只多送出約一個裝置週期的舊資料（不丟會多出整個緩衝）
    CHECK(played / frameBytes - heard < 4410);
    CHECK(engine.underruns() == 0);
}

TEST_CASE("AC-11-2：沒在播或已經解完時跳轉回傳 false，由上層重來") {
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CHECK_FALSE(engine.seek(0));
    CaptureBackend backend(fastCapture());
    // 0.1 秒的曲目在 play() 先填緩衝時就解完了
    REQUIRE(engine.play(EngineTrack{source(makeNoise(cd(), 4410, 5)), 0, -1, 1}, backend));
    CHECK_FALSE(engine.seek(100));
    engine.stop();
}

TEST_CASE("AC-11-2：撤回預開的下一首後重新要，接上的是新的那首，問的一律是正在解碼的那首之後") {
    const QByteArray first = makeNoise(cd(), 44100, 1);
    const QByteArray old = makeNoise(cd(), 22050, 2);
    const QByteArray replaced = makeNoise(cd(), 22050, 3);
    SwitchingProvider provider;
    provider.offer(old, 2);
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setNextTrackProvider(&provider);
    CaptureBackend::Options options = fastCapture();
    options.speed = 1.0;
    CaptureBackend backend(options);
    // 1 秒的曲目不到 5 秒，play() 先填緩衝時就問了下一首
    REQUIRE(engine.play(EngineTrack{source(first), 0, -1, 1}, backend));
    REQUIRE(provider.asked().size() == 1);

    provider.offer(replaced, 3);
    engine.invalidateNextTrack();
    REQUIRE(waitUntil([&] { return provider.asked().size() == 2; }));
    provider.offer(std::nullopt, 0);
    REQUIRE(waitUntil([&] { return engine.isFinished(); }));
    CHECK(engine.position().token == 3);
    engine.stop();

    CHECK(provider.asked() == std::vector<quint64>{1, 1, 3});
    CHECK(startsWithThenSilence(backend.captured(), first + replaced));
}

namespace {

/// 音源 seek 要一段時間（實際的 FLAC 在大檔裡找位置也要幾十毫秒），讓「已要求、還沒處理完」
/// 這段時間長到測試一定讀得到位置。
class SlowSeekSource final : public auralis::IAudioSource {
public:
    SlowSeekSource(AudioFormat format, QByteArray data) : m_inner(format, std::move(data)) {}
    AudioFormat format() const override { return m_inner.format(); }
    qint64 lengthFrames() const override { return m_inner.lengthFrames(); }
    auralis::Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        return m_inner.read(dest, maxFrames);
    }
    auralis::Result<qint64> seek(qint64 frame) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        return m_inner.seek(frame);
    }

private:
    MemorySource m_inner;
};

/// 播 4 秒的曲目（startSeconds 起的區段，模擬 CUE 曲目），1 秒後跳到區段的第 3 秒：
/// 處理跳轉的期間位置不會回報舊位置；播完時位置正好是區段長度（跳轉後回報的位置就是送出的樣本框數）；
/// 跳轉後送出的資料是音源在「區段起點＋3 秒」之後的那一段。
void checkSeekPosition(const AudioFormat& format, int startSeconds) {
    const qint64 rate = format.rate;
    const QByteArray data = makeNoise(format, static_cast<int>(rate * (startSeconds + 4)), 47);
    const qint64 start = rate * startSeconds;
    const qint64 end = start + rate * 4;
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend::Options options = fastCapture();
    options.speed = 2.0;
    options.maxCaptureSeconds = 12;
    CaptureBackend backend(options);
    REQUIRE(engine.play(EngineTrack{std::make_unique<SlowSeekSource>(format, data), start, end, 6},
                        backend));
    REQUIRE(waitUntil([&] { return engine.position().frames > rate / 10; }));

    const qint64 target = rate * 3;
    REQUIRE(engine.seek(target));
    // 音源 seek 要 120 毫秒；這段時間（以及之後）一直讀位置，模擬服務每 50 毫秒的輪詢
    qint64 lowest = target;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < until) {
        const PlaybackEngine::Position position = engine.position();
        CHECK(position.token == 6);
        lowest = std::min(lowest, position.frames);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(lowest == target);
    REQUIRE(waitUntil([&] { return engine.isFinished(); }));
    CHECK(engine.position().frames == end - start);
    engine.stop();

    const int frameBytes = format.bytesPerFrame();
    const QByteArray tail =
        data.mid((start + target) * frameBytes, (end - start - target) * frameBytes);
    const QByteArray captured = backend.captured();
    const qsizetype at = captured.indexOf(tail.left(4096));
    REQUIRE(at > 0);
    CHECK(startsWithThenSilence(captured.mid(at), tail));
    CHECK(engine.underruns() == 0);
}

} // namespace

TEST_CASE("AC-11-2：跳轉處理完之前位置不回報舊位置，之後的位置與送出的樣本框對得上（44.1 kHz）") {
    checkSeekPosition(cd(), 0);
}

TEST_CASE("AC-11-2：192 kHz／24 位元的 CUE 區段跳轉，位置從區段起點算、不回報舊位置") {
    AudioFormat format;
    format.rate = 192000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    checkSeekPosition(format, 2);
}
