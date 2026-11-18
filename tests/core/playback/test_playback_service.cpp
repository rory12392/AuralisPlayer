// PlaybackService（AC-11-2）：佇列操作、重複與隨機、傳輸控制與狀態訊號，用 Capture 後端與
// 記憶體音源驗證實際送進裝置的資料順序。
#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "playback/playback_service_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSignalSpy>

#include <functional>
#include <map>

using auralis::AudioFormat;
using auralis::PlaybackService;
using auralis::QueueEntry;
using auralis::TrackRef;
using auralis::core::CaptureBackend;
using auralis::core::OutputRegistryImpl;
using auralis::core::PlaybackServiceImpl;
using auralis::core::ProcessorChainImpl;
using auralis::core::SourceRegistryImpl;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using State = PlaybackService::State;
using RepeatMode = PlaybackService::RepeatMode;

namespace {

AudioFormat cd() {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    return format;
}

/// 副檔名 .mem 的音源：路徑對到測試準備好的資料；沒有的路徑開不了（AC-01-3）。
class MemoryFactory final : public auralis::ISourceFactory {
public:
    QString name() const override { return QStringLiteral("記憶體"); }
    QStringList extensions() const override { return {QStringLiteral("mem")}; }
    auralis::Result<std::unique_ptr<auralis::IAudioSource>> open(const QString& path) override {
        const auto it = tracks.find(path);
        if (it == tracks.end()) {
            return auralis::Error{QStringLiteral("找不到檔案"), 0, QStringLiteral("open")};
        }
        return std::unique_ptr<auralis::IAudioSource>(
            std::make_unique<MemorySource>(cd(), it->second));
    }
    std::map<QString, QByteArray> tracks;
};

/// 測試用的服務：記憶體音源、Capture 後端（4 倍速）、固定的隨機 seed。
struct Fixture {
    Fixture() {
        if (QCoreApplication::instance() == nullptr) {
            static int argc = 1;
            static char name[] = "auralis_core_tests";
            static char* argv[] = {name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        auto capture = std::make_unique<CaptureBackend>(fastCapture());
        backend = capture.get();
        service.setOutputBackend(std::move(capture));
    }

    /// 加一首 seconds 秒的雜訊，回傳它的 TrackRef。
    TrackRef add(const QString& name, double seconds, quint32 seed) {
        const QString path = name + QStringLiteral(".mem");
        factory->tracks[path] = makeNoise(cd(), static_cast<int>(44100 * seconds), seed);
        return TrackRef{path, 0};
    }
    QByteArray data(const TrackRef& track) const { return factory->tracks.at(track.path); }

    /// 處理事件並輪詢服務，直到 condition 成立或逾時。
    bool drive(const std::function<bool()>& condition, int timeoutMs = 20000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents();
            service.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        QCoreApplication::processEvents();
        return condition();
    }
    bool driveUntilStopped() {
        return drive([this] { return service.state() == State::Stopped; });
    }

    std::shared_ptr<MemoryFactory> factory = std::make_shared<MemoryFactory>();
    SourceRegistryImpl sources{factory};
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors, 17};
    CaptureBackend* backend = nullptr;
};

QStringList paths(const QList<QueueEntry>& entries) {
    QStringList list;
    for (const QueueEntry& entry : entries) {
        list.append(entry.track.path);
    }
    return list;
}

} // namespace

TEST_CASE("AC-11-2：照佇列順序無縫接續；預開之後「接著播放」插入的那首先播") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1);
    const TrackRef c = f.add("c", 0.5, 3);
    const TrackRef b = f.add("b", 0.5, 2);
    QSignalSpy tracks(&f.service, &PlaybackService::trackChanged);
    f.service.playFrom({a, c}, 0);
    REQUIRE(f.service.state() == State::Playing);
    // a 不到 5 秒，開始播時就已經預開了 c；插入 b 要撤回 c
    f.service.enqueueNext({b});
    CHECK(paths(f.service.queue()) == QStringList{a.path, b.path, c.path});
    REQUIRE(f.driveUntilStopped());
    CHECK(f.backend->sessions().size() == 1);
    CHECK(startsWithThenSilence(f.backend->captured(), f.data(a) + f.data(b) + f.data(c)));
    CHECK(tracks.count() == 3);
    CHECK(f.service.currentEntry()->track.path == c.path);
}

TEST_CASE("AC-11-2：加到最後的曲目在目前這首之後無縫接著播") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1);
    const TrackRef z = f.add("z", 0.5, 9);
    f.service.playFrom({a}, 0);
    f.service.enqueueLast({z});
    REQUIRE(f.driveUntilStopped());
    CHECK(f.backend->sessions().size() == 1);
    CHECK(startsWithThenSilence(f.backend->captured(), f.data(a) + f.data(z)));
}

TEST_CASE("AC-11-2：目前這首已經整首解完才加到最後，播完後仍接著播（重開裝置）") {
    Fixture f;
    // 0.5 秒的曲目在開始播時就整首寫進緩衝、解碼結束，來不及無縫接上
    const TrackRef a = f.add("a", 0.5, 1);
    const TrackRef z = f.add("z", 0.5, 9);
    f.service.playFrom({a}, 0);
    f.service.enqueueLast({z});
    REQUIRE(f.driveUntilStopped());
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 2);
    CHECK(startsWithThenSilence(sessions[0].data, f.data(a)));
    CHECK(startsWithThenSilence(sessions[1].data, f.data(z)));
}

TEST_CASE("AC-11-2：重複全部播完最後一首回到第一首") {
    Fixture f;
    const TrackRef a = f.add("a", 0.5, 1);
    const TrackRef b = f.add("b", 0.5, 2);
    f.service.setRepeatMode(RepeatMode::All);
    f.service.playFrom({a, b}, 0);
    const QByteArray expected = f.data(a) + f.data(b) + f.data(a);
    REQUIRE(f.drive(
        [&] { return f.backend->sourceFrames() * cd().bytesPerFrame() > expected.size(); }));
    f.service.stop();
    CHECK(f.backend->captured().startsWith(expected));
}

TEST_CASE("AC-11-2：重複單曲自動重播同一首，每次重播都發 trackChanged") {
    Fixture f;
    const TrackRef a = f.add("a", 0.5, 1);
    const TrackRef b = f.add("b", 0.5, 2);
    QSignalSpy tracks(&f.service, &PlaybackService::trackChanged);
    f.service.setRepeatMode(RepeatMode::One);
    f.service.playFrom({a, b}, 0);
    const QByteArray expected = f.data(a) + f.data(a);
    REQUIRE(f.drive(
        [&] { return f.backend->sourceFrames() * cd().bytesPerFrame() > expected.size(); }));
    f.service.stop();
    CHECK(f.backend->captured().startsWith(expected));
    CHECK(tracks.count() >= 2);
    CHECK(f.service.currentEntry()->track.path == a.path);
}

TEST_CASE("AC-11-2：隨機播放從選的那首開始，實際播放順序就是佇列顯示的順序") {
    Fixture f;
    QList<TrackRef> list;
    for (int i = 0; i < 6; ++i) {
        list.append(f.add(QStringLiteral("s%1").arg(i), 0.25, 40 + i));
    }
    f.service.setShuffle(true);
    f.service.playFrom(list, 2);
    const QList<QueueEntry> order = f.service.queue();
    REQUIRE(order.size() == 6);
    CHECK(order.first().track.path == list[2].path);
    QByteArray expected;
    for (const QueueEntry& entry : order) {
        expected += f.data(entry.track);
    }
    REQUIRE(f.driveUntilStopped());
    CHECK(startsWithThenSilence(f.backend->captured(), expected));
}

TEST_CASE("AC-11-2：跳轉不重開裝置，位置訊號從新位置繼續") {
    Fixture f;
    const TrackRef a = f.add("a", 4.0, 5);
    QSignalSpy positions(&f.service, &PlaybackService::positionChanged);
    f.service.playFrom({a}, 0);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 200; }));
    const qint64 heard = f.service.engine().position().frames;
    f.service.seek(3000);
    CHECK(f.service.positionMs() == 3000);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 3100; }));
    REQUIRE(f.driveUntilStopped());
    CHECK(f.backend->sessions().size() == 1);
    const QByteArray captured = f.backend->captured();
    const QByteArray tail = f.data(a).mid(44100 * 3 * cd().bytesPerFrame());
    const qsizetype at = captured.indexOf(tail.left(4096));
    REQUIRE(at > 0);
    CHECK(startsWithThenSilence(captured.mid(at), tail));
    // 跳轉之後幾乎沒有再送出舊位置的資料（緩衝被丟掉；不丟會多出整個緩衝約 0.7 秒）
    qsizetype played = at;
    while (played > 0 && captured[played - 1] == 0) {
        --played;
    }
    CHECK(played / cd().bytesPerFrame() - heard < 4410);
    CHECK(positions.count() > 3);
    CHECK(f.service.engine().underruns() == 0);
}

TEST_CASE("AC-11-2：暫停關掉裝置，繼續時從暫停的位置重開") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0, 6);
    QSignalSpy states(&f.service, &PlaybackService::stateChanged);
    f.service.playFrom({a}, 0);
    REQUIRE(f.drive([&] { return f.service.positionMs() > 500; }));
    f.service.pause();
    CHECK(f.service.state() == State::Paused);
    const qint64 paused = f.service.positionMs();
    f.service.togglePause();
    CHECK(f.service.state() == State::Playing);
    REQUIRE(f.driveUntilStopped());
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 2);
    const qint64 frame = paused * 44100 / 1000;
    CHECK(startsWithThenSilence(sessions[1].data, f.data(a).mid(frame * cd().bytesPerFrame())));
    REQUIRE(states.count() == 4);
    CHECK(states[0][0].value<State>() == State::Playing);
    CHECK(states[1][0].value<State>() == State::Paused);
    CHECK(states[2][0].value<State>() == State::Playing);
    CHECK(states[3][0].value<State>() == State::Stopped);
}

TEST_CASE("AC-01-3：開不了的曲目跳過並通知，接著播下一首") {
    Fixture f;
    const TrackRef a = f.add("a", 0.5, 1);
    const TrackRef b = f.add("b", 0.5, 2);
    const TrackRef missing{QStringLiteral("missing.mem"), 0};
    QSignalSpy skipped(&f.service, &PlaybackService::trackSkipped);
    f.service.playFrom({a, missing, b}, 0);
    REQUIRE(f.driveUntilStopped());
    CHECK(startsWithThenSilence(f.backend->captured(), f.data(a) + f.data(b)));
    REQUIRE(skipped.count() == 1);
    CHECK(skipped[0][0].value<TrackRef>().path == missing.path);
    CHECK(f.service.queue()[1].unplayable);
}

TEST_CASE("AC-11-2：移出正在播的曲目時接著播它後面那首；上一首與下一首") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0, 1);
    const TrackRef b = f.add("b", 3.0, 2);
    const TrackRef c = f.add("c", 0.5, 3);
    f.service.playFrom({a, b, c}, 0);
    f.service.next();
    CHECK(f.service.currentEntry()->track.path == b.path);
    f.service.previous();
    CHECK(f.service.currentEntry()->track.path == a.path);
    f.service.remove({f.service.currentEntry()->id});
    CHECK(paths(f.service.queue()) == QStringList{b.path, c.path});
    CHECK(f.service.currentEntry()->track.path == b.path);
    CHECK(f.service.state() == State::Playing);
    f.service.move(1, 0);
    CHECK(paths(f.service.queue()) == QStringList{c.path, b.path});
    CHECK(f.service.currentIndex() == 1);
    f.service.clear();
    CHECK(paths(f.service.queue()) == QStringList{b.path});
    f.service.stop();
    CHECK(f.service.state() == State::Stopped);
    CHECK(f.service.positionMs() == 0);
}
