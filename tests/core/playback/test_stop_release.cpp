// 「停止」（AC-02-8，T-95）：PlaybackService::stop() 關閉輸出裝置，獨占的裝置讓出來給其他程式；
// 佇列與目前曲目保留，再按播放從目前曲目開頭播。獨占用 DeviceState::busy 模擬：開著的後端佔住
// 裝置，close() 才放開，其他後端（代表別的程式）在佔住期間開啟會得到「被佔用」。
#include "output_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {

/// 以獨占方式開裝置的測試後端：開啟成功就佔住 DeviceState，close() 時放開；記下開關次數。
class ExclusiveBackend final : public auralis::IOutputBackend {
public:
    explicit ExclusiveBackend(DeviceState& state) : m_state(state), m_inner(state) {}

    QString name() const override { return QStringLiteral("測試獨占"); }
    auralis::Result<AudioFormat> open(const AudioFormat& requested) override {
        auto opened = m_inner.open(requested);
        if (opened) {
            m_state.busy = true;
            m_held = true;
            ++opens;
        }
        return opened;
    }
    auralis::Result<bool> start(auralis::IRenderSource& source) override {
        return m_inner.start(source);
    }
    void stop() noexcept override { m_inner.stop(); }
    void close() noexcept override {
        m_inner.close();
        if (m_held) {
            m_held = false;
            m_state.busy = false;
            ++closes;
        }
    }
    OutputErrorKind lastErrorKind() const noexcept override { return m_inner.lastErrorKind(); }

    bool held() const { return m_held; }
    const CaptureBackend& capture() const { return m_inner.capture(); }

    int opens = 0;
    int closes = 0;

private:
    DeviceState& m_state;
    TestBackend m_inner;
    bool m_held = false;
};

} // namespace

TEST_CASE("AC-02-8（T-95）：停止關閉輸出裝置，同一裝置可以再以獨占開啟；按播放從目前曲目開頭播") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0);
    const TrackRef b = f.add("b", 1.0);
    DeviceState& device = f.device("a");
    auto owned = std::make_unique<ExclusiveBackend>(device);
    ExclusiveBackend* backend = owned.get();
    f.service.setOutputBackend(std::move(owned));

    f.service.playFrom({a, b}, 0);
    f.service.waitForStart();
    REQUIRE(f.drive([&] { return f.service.positionMs() > 500; }));
    REQUIRE(backend->held());

    // 播放中：別的程式開不了這個裝置
    ExclusiveBackend other(device);
    REQUIRE_FALSE(other.open(cd()).hasValue());
    CHECK(other.lastErrorKind() == OutputErrorKind::DeviceBusy);

    f.service.stop();
    CHECK(f.service.state() == State::Stopped);
    CHECK(f.service.positionMs() == 0);
    CHECK(f.service.outputStatus().error.isEmpty());
    // 輸出後端已 close，裝置放開
    CHECK_FALSE(backend->held());
    CHECK(backend->closes == backend->opens);
    CHECK_FALSE(device.busy);
    // 佇列與目前曲目保留
    REQUIRE(f.service.queue().size() == 2);
    REQUIRE(f.service.currentEntry().has_value());
    CHECK(f.service.currentEntry()->track.path == a.path);

    // 停止後：別的程式可以馬上以獨占開啟同一個裝置；用完放開
    REQUIRE(other.open(cd()).hasValue());
    other.close();
    CHECK_FALSE(device.busy);

    // 再按播放：從目前曲目（a）的開頭播，裝置重新開啟
    f.service.play();
    f.service.waitForStart();
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.currentEntry()->track.path == a.path);
    REQUIRE(f.drive([&] { return f.service.state() == State::Stopped; }));
    const auto sessions = backend->capture().sessions();
    REQUIRE(sessions.size() >= 2);
    CHECK(startsWithThenSilence(sessions[1].data, f.sourceFactory->tracks.at(a.path).second +
                                                      f.sourceFactory->tracks.at(b.path).second));
    CHECK(backend->opens == 2);
}

TEST_CASE("AC-02-8（T-95）：暫停中按停止，位置回到 0，再按播放從開頭播") {
    Fixture f;
    const TrackRef a = f.add("a", 3.0);
    auto owned = std::make_unique<ExclusiveBackend>(f.device("a"));
    ExclusiveBackend* backend = owned.get();
    f.service.setOutputBackend(std::move(owned));

    f.service.playFrom({a}, 0);
    f.service.waitForStart();
    REQUIRE(f.drive([&] { return f.service.positionMs() > 500; }));
    f.service.pause();
    REQUIRE(f.service.state() == State::Paused);
    f.service.stop();
    CHECK(f.service.state() == State::Stopped);
    CHECK(f.service.positionMs() == 0);
    CHECK_FALSE(f.device("a").busy);

    f.service.play();
    f.service.waitForStart();
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.positionMs() == 0);
    f.service.stop();
    CHECK_FALSE(backend->held());
}
