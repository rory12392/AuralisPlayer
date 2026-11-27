// 開始播放不卡 UI 執行緒（T-113，Architecture §5 的 16 毫秒）：開檔、探測格式與預填緩衝在背景，
// UI 執行緒只送出要求。慢的音源（開檔要等）模擬大檔或慢磁碟；開檔中再按播放別首、停止、跳轉、
// 暫停、關掉服務時，以最後一次操作為準，舊的要求晚到也不蓋掉新的。
#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "playback/playback_service_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>

using auralis::AudioFormat;
using auralis::PlaybackService;
using auralis::TrackRef;
using auralis::core::CaptureBackend;
using auralis::core::OutputRegistryImpl;
using auralis::core::PlaybackServiceImpl;
using auralis::core::ProcessorChainImpl;
using auralis::core::SourceRegistryImpl;
using auralis::test::cd;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using State = PlaybackService::State;
using Clock = std::chrono::steady_clock;

namespace {

/// UI 執行緒一次操作的上限（Architecture §5）。
constexpr qint64 kUiBudgetMs = 16;
/// 慢磁碟的開檔時間。
constexpr int kSlowOpenMs = 200;

/// 副檔名 .mem 的音源：開檔前等這個路徑設定的毫秒數（模擬慢磁碟），記下每個路徑開了幾次。
class SlowFactory final : public auralis::ISourceFactory {
public:
    QString name() const override { return QStringLiteral("慢速記憶體"); }
    QStringList extensions() const override { return {QStringLiteral("mem")}; }
    auralis::Result<std::unique_ptr<auralis::IAudioSource>> open(const QString& path) override {
        int delay = 0;
        QByteArray data;
        {
            const std::lock_guard lock(m_mutex);
            ++m_opens[path];
            delay = delays[path];
            data = tracks.at(path);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        return std::unique_ptr<auralis::IAudioSource>(std::make_unique<MemorySource>(cd(), data));
    }
    int opens(const QString& path) {
        const std::lock_guard lock(m_mutex);
        return m_opens[path];
    }
    // 測試在開始播放前填好；之後只讀
    std::map<QString, QByteArray> tracks;
    std::map<QString, int> delays;

private:
    std::mutex m_mutex;
    std::map<QString, int> m_opens;
};

/// 記下狀態訊號的順序（UI 與 SMTC 依這個順序更新）。
struct SignalLog {
    explicit SignalLog(PlaybackService& service) {
        QObject::connect(&service, &PlaybackService::stateChanged, &context,
                         [this] { names << QStringLiteral("state"); });
        QObject::connect(&service, &PlaybackService::trackChanged, &context,
                         [this] { names << QStringLiteral("track"); });
        QObject::connect(&service, &PlaybackService::queueChanged, &context,
                         [this] { names << QStringLiteral("queue"); });
        QObject::connect(&service, &PlaybackService::positionChanged, &context,
                         [this] { names << QStringLiteral("position"); });
        QObject::connect(&service, &PlaybackService::outputStatusChanged, &context,
                         [this] { names << QStringLiteral("output"); });
        QObject::connect(&service, &PlaybackService::trackSkipped, &context,
                         [this] { names << QStringLiteral("skipped"); });
    }
    QObject context;
    QStringList names;
};

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
        service->setOutputBackend(std::move(capture));
    }

    TrackRef add(const QString& name, double seconds, quint32 seed, int openDelayMs = 0) {
        const QString path = name + QStringLiteral(".mem");
        factory->tracks[path] = makeNoise(cd(), static_cast<int>(44100 * seconds), seed);
        factory->delays[path] = openDelayMs;
        return TrackRef{path, 0};
    }
    QByteArray data(const TrackRef& track) const { return factory->tracks.at(track.path); }

    bool drive(const std::function<bool()>& condition, int timeoutMs = 20000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!condition() && Clock::now() < deadline) {
            QCoreApplication::processEvents();
            if (service) {
                service->poll();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        QCoreApplication::processEvents();
        return condition();
    }
    /// 處理事件 ms 毫秒（讓背景的要求有時間完成或被丟掉）。
    void settle(int ms) {
        drive([] { return false; }, ms);
    }

    std::shared_ptr<SlowFactory> factory = std::make_shared<SlowFactory>();
    SourceRegistryImpl sources{factory};
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    std::unique_ptr<PlaybackServiceImpl> service =
        std::make_unique<PlaybackServiceImpl>(sources, outputs, processors, 17);
    CaptureBackend* backend = nullptr;
};

/// fn 在呼叫端（UI）執行緒花了幾毫秒。
qint64 elapsedMs(const std::function<void()>& fn) {
    const auto start = Clock::now();
    fn();
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

/// 每秒的樣本位元組數（cd()）。
constexpr qint64 kBytesPerSecond = 44100 * 4;

} // namespace

TEST_CASE("AC-11-2（T-113）：開檔要 200 毫秒時，開始播放與下一首在 UI 執行緒都不超過 16 毫秒") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0, 1, kSlowOpenMs);
    const TrackRef b = f.add("b", 1.0, 2, kSlowOpenMs);
    const qint64 playMs = elapsedMs([&] { f.service->playFrom({a, b}, 0); });
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    const qint64 nextMs = elapsedMs([&] { f.service->next(); });
    REQUIRE(f.drive([&] { return f.service->currentIndex() == 1; }));
    // 量測值寫進回報與 metrics.csv（-s 或直接執行時看得到）
    std::cout << "T-113 UI 執行緒耗時：playFrom " << playMs << " ms、next " << nextMs << " ms\n";
    CHECK(playMs < kUiBudgetMs);
    CHECK(nextMs < kUiBudgetMs);
    REQUIRE(f.drive([&] { return f.service->state() == State::Stopped; }));
    // 位元完美照舊：a 被切掉，b 從頭完整送出
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 2);
    CHECK(f.data(a).startsWith(sessions[0].data.left(kBytesPerSecond / 10)));
    CHECK(startsWithThenSilence(sessions[1].data, f.data(b)));
}

TEST_CASE("AC-11-2（T-113）：開檔中又播放別首，只播後來那首，先開的那首晚到也不蓋掉") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 300);
    const TrackRef b = f.add("b", 0.5, 2);
    SignalLog log(*f.service);
    f.service->playFrom({a, b}, 0);
    f.service->playAt(1);
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    CHECK(f.service->currentEntry()->track.path == b.path);
    REQUIRE(f.drive([&] { return f.service->state() == State::Stopped; }));
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(startsWithThenSilence(sessions[0].data, f.data(b)));
    CHECK(log.names.count(QStringLiteral("track")) == 1);
}

TEST_CASE("AC-02-8（T-113）：開檔中按停止，不開裝置、維持停止，停止本身不等開檔") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 300);
    f.service->playFrom({a}, 0);
    const qint64 stopMs = elapsedMs([&] { f.service->stop(); });
    CHECK(stopMs < kUiBudgetMs);
    CHECK(f.service->state() == State::Stopped);
    f.settle(600);
    CHECK(f.service->state() == State::Stopped);
    CHECK(f.backend->sessions().empty());
    // 之後再按播放照常開始
    f.service->play();
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
}

TEST_CASE("AC-11-2（T-113）：開檔中暫停就取消這次開始；再按播放照常開始") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 300);
    f.service->playFrom({a}, 0);
    f.service->togglePause();
    f.settle(600);
    CHECK(f.service->state() != State::Playing);
    CHECK(f.backend->sessions().empty());
    f.service->togglePause();
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    CHECK(f.service->currentEntry()->track.path == a.path);
}

TEST_CASE("AC-11-2（T-113）：開檔中跳轉，從跳轉的位置開始播") {
    Fixture f;
    const TrackRef a = f.add("a", 2.0, 1, 300);
    f.service->playFrom({a}, 0);
    f.service->seek(1000);
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    CHECK(f.service->positionMs() >= 1000);
    REQUIRE(f.drive([&] { return f.service->state() == State::Stopped; }));
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(startsWithThenSilence(sessions[0].data, f.data(a).mid(kBytesPerSecond)));
}

TEST_CASE("AC-11-2（T-113）：開檔中連按兩次下一首，跳到後面第二首") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 300);
    const TrackRef b = f.add("b", 1.0, 2, 300);
    const TrackRef c = f.add("c", 0.5, 3);
    f.service->playFrom({a, b, c}, 0);
    f.service->next();
    f.service->next();
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    CHECK(f.service->currentEntry()->track.path == c.path);
    REQUIRE(f.drive([&] { return f.service->state() == State::Stopped; }));
    const auto sessions = f.backend->sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(startsWithThenSilence(sessions[0].data, f.data(c)));
}

TEST_CASE("AC-11-2（T-113）：開檔中關掉服務（關程式）不當掉，最多等一次開檔就回來") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 300);
    f.service->playFrom({a}, 0);
    // 等背景真的開始開檔，關掉時才是「開檔中」
    REQUIRE(f.drive([&] { return f.factory->opens(a.path) == 1; }));
    const qint64 closeMs = elapsedMs([&] { f.service.reset(); });
    std::cout << "T-113 開檔中關掉服務：" << closeMs << " ms\n";
    CHECK(closeMs < 1000);
    f.settle(100);
    CHECK(f.factory->opens(a.path) == 1);
}

TEST_CASE("AC-11-2（T-125）：播放中按下一首、開檔還沒完成時清空佇列，留下要開的那首並接著播它") {
    Fixture f;
    const TrackRef a = f.add("a", 8.0, 1); // 剩 5 秒才預開下一首：按下一首時還沒預開 b
    const TrackRef b = f.add("b", 1.0, 2, 300);
    const TrackRef c = f.add("c", 0.5, 3);
    f.service->playFrom({a, b, c}, 0);
    REQUIRE(f.drive([&] { return f.service->state() == State::Playing; }));
    REQUIRE(f.factory->opens(b.path) == 0);
    f.service->next();
    // 等背景真的開始開 b，清空時才是「開始中」
    REQUIRE(f.drive([&] { return f.factory->opens(b.path) == 1; }));
    f.service->clear();
    f.service->waitForStart();
    CHECK(f.service->state() == State::Playing);
    REQUIRE(f.service->currentEntry().has_value());
    CHECK(f.service->currentEntry()->track.path == b.path);
    REQUIRE(f.service->queue().size() == 1);
    CHECK(f.service->queue().constFirst().track.path == b.path);
    f.service->stop();
}

TEST_CASE("AC-11-2（T-125）：從停止開始播短曲、預填已預開下一首時「接著播放」，下一首是新插入的") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1);
    const TrackRef c = f.add("c", 0.5, 3, 300);
    const TrackRef b = f.add("b", 0.5, 2);
    QStringList played;
    QObject::connect(f.service.get(), &PlaybackService::trackChanged, f.service.get(),
                     [&] { played << f.service->currentEntry()->track.path; });
    f.service->playFrom({a, c}, 0);
    // a 不到 5 秒：開始播放的預填就會預開 c；等它真的在開 c（開始還沒完成）再插入 b
    REQUIRE(f.drive([&] { return f.factory->opens(c.path) == 1; }));
    REQUIRE(f.service->state() != State::Playing);
    f.service->enqueueNext({b});
    f.service->waitForStart();
    REQUIRE(f.drive([&] { return f.service->state() == State::Stopped; }));
    const auto sessions = f.backend->sessions();
    // 同一個裝置工作階段裡依 a、b、c 接續，沒有照佇列重來（重來會開第二個工作階段）。
    // c 開檔慢，b 播放中預開 c 時會欠載，所以不比對 a 之後的樣本
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].data.startsWith(f.data(a)));
    CHECK(played == QStringList{a.path, b.path, c.path});
}

TEST_CASE("AC-11-2（T-113）：狀態訊號的順序照舊：開始播放 state、track、queue、position、output") {
    Fixture f;
    const TrackRef a = f.add("a", 1.0, 1, 50);
    const TrackRef b = f.add("b", 1.0, 2, 50);
    SignalLog log(*f.service);
    f.service->playFrom({a, b}, 0);
    // 送出要求時不發訊號，開好了才一次照原本的順序發
    CHECK(log.names.isEmpty());
    f.service->waitForStart();
    REQUIRE(f.service->state() == State::Playing);
    std::cout << "T-113 訊號（開始播放）：" << log.names.join(u',').toStdString() << "\n";
    CHECK(log.names == QStringList{QStringLiteral("state"), QStringLiteral("track"),
                                   QStringLiteral("queue"), QStringLiteral("position"),
                                   QStringLiteral("output")});
    log.names.clear();
    f.service->next();
    f.service->waitForStart();
    REQUIRE(f.service->currentIndex() == 1);
    std::cout << "T-113 訊號（下一首）：" << log.names.join(u',').toStdString() << "\n";
    // 同格式換首時輸出狀態沒變，不發 outputStatusChanged
    CHECK(log.names == QStringList{QStringLiteral("track"), QStringLiteral("queue"),
                                   QStringLiteral("position")});
    f.service->stop();
}
