// PlaybackService 播 CUE 曲目（AC-07-2）：佇列裡的 CUE 曲目（身分是 .cue 路徑加軌號）
// 照 .cue 的區段播放，相鄰兩軌接起來等於整軌檔的那一段。用 Capture 後端逐位元比對。
#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "../library/cue_fixtures.h"
#include "../library/library_fixtures.h"
#include "playback/playback_service_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

using auralis::AudioFormat;
using auralis::PlaybackService;
using auralis::TrackRef;
using namespace auralis::core;
using namespace auralis::test;
using State = PlaybackService::State;

namespace {

/// 處理事件並輪詢服務，直到 condition 成立或逾時。
bool drive(PlaybackServiceImpl& service, const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    // 開始播放在背景開檔（T-113）：先等它做完，條件才不會在開始之前就成立
    service.waitForStart();
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        service.poll();
        service.waitForStart();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QCoreApplication::processEvents();
    return condition();
}

} // namespace

TEST_CASE("AC-07-2：佇列裡相鄰的兩首 CUE 曲目經 PlaybackService 播放，等於整軌檔的對應區段") {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_core_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
    // 5 秒的整軌 FLAC（44.1 kHz／16 位元雜訊）加樣本 .cue（三軌）
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    const QString dir = freshTestDir(QStringLiteral("t-10-cue-queue"));
    const QByteArray pcm = makeNoise(format, 44100 * kSampleAudioSeconds, 1016);
    const QString why =
        encodeFile(dir + QStringLiteral("\\album.flac"), "flac", "flac", format, pcm);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    const QString cuePath = dir + QStringLiteral("\\album.cue");
    writeBytes(cuePath, cueFixture(QStringLiteral("utf8-bom.cue")));

    SourceRegistryImpl sources;
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors, 17};
    auto capture = std::make_unique<CaptureBackend>(fastCapture());
    CaptureBackend* backend = capture.get();
    service.setOutputBackend(std::move(capture));

    service.playFrom({TrackRef{cuePath, 1}, TrackRef{cuePath, 2}}, 0);
    service.waitForStart();
    REQUIRE(service.state() == State::Playing);
    CHECK(service.queue().size() == 2);
    CHECK_FALSE(service.queue().at(0).unplayable);
    // 第 1 軌是 0–150 個 CD 影格，長度是這一段而不是整軌檔
    CHECK(service.durationMs() == 2000);
    REQUIRE(drive(service, [&service] { return service.state() == State::Stopped; }));

    const qint64 end = static_cast<qint64>(kSampleTrackStart[2]) * 44100 / 75;
    CHECK(backend->sessions().size() == 1); // 同一個整軌檔的相鄰區段不重開裝置
    CHECK(service.engine().underruns() == 0);
    CHECK(startsWithThenSilence(backend->captured(), pcm.left(end * format.bytesPerFrame())));
    CHECK_FALSE(service.queue().at(1).unplayable);
    CHECK(service.currentEntry()->track.subsong == 2);
}

TEST_CASE("AC-11-2：192 kHz CUE 曲目跳轉，位置只往前，送出的是區段起點加跳轉位置之後的資料") {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_core_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
    AudioFormat format;
    format.rate = 192000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    const QString dir = freshTestDir(QStringLiteral("fix-seek-cue"));
    const QByteArray pcm = makeNoise(format, 192000 * kSampleAudioSeconds, 1017);
    const QString why =
        encodeFile(dir + QStringLiteral("\\album.flac"), "flac", "flac", format, pcm);
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
    const QString cuePath = dir + QStringLiteral("\\album.cue");
    writeBytes(cuePath, cueFixture(QStringLiteral("utf8-bom.cue")));

    SourceRegistryImpl sources;
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors, 17};
    auto capture = std::make_unique<CaptureBackend>(fastCapture());
    CaptureBackend* backend = capture.get();
    service.setOutputBackend(std::move(capture));
    QList<qint64> positions;
    QObject::connect(&service, &PlaybackService::positionChanged,
                     [&positions](qint64 positionMs) { positions.append(positionMs); });

    // 第 2 軌是 150–262 個 CD 影格（約 1.49 秒）
    service.playFrom({TrackRef{cuePath, 2}}, 0);
    service.waitForStart();
    REQUIRE(service.state() == State::Playing);
    REQUIRE(drive(service, [&service] { return service.positionMs() > 100; }));
    positions.clear();
    service.seek(1000);
    REQUIRE(drive(service, [&service] { return service.state() == State::Stopped; }));

    // 跳轉之後送出的位置從 1000 開始只往前，不會先跳回舊位置；最後一個接近區段長度
    const qint64 durationMs = (kSampleTrackStart[2] - kSampleTrackStart[1]) * 1000 / 75;
    qint64 last = 1000;
    qint64 highest = 0;
    for (const qint64 position : positions) {
        if (position == 0) {
            break; // 播完停止時歸零
        }
        CHECK(position >= last);
        last = position;
        highest = std::max(highest, position);
    }
    CHECK(highest <= durationMs);
    CHECK(highest >= durationMs - 50);

    const qint64 start = static_cast<qint64>(kSampleTrackStart[1]) * 192000 / 75;
    const qint64 end = static_cast<qint64>(kSampleTrackStart[2]) * 192000 / 75;
    const int frameBytes = format.bytesPerFrame();
    const QByteArray tail =
        pcm.mid((start + 192000) * frameBytes, (end - start - 192000) * frameBytes);
    const QByteArray captured = backend->captured();
    const qsizetype at = captured.indexOf(tail.left(4096));
    REQUIRE(at > 0);
    CHECK(startsWithThenSilence(captured.mid(at), tail));
    CHECK(backend->sessions().size() == 1);
}
