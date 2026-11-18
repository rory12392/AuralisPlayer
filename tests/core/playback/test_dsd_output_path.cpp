// T-49：DSD 包裝器與輸出路徑的相容（AC-04-2）——換輸出路徑前通知模組換包裝器、
// DoP 遇到會改動訊號的路徑時核心不送出；以及處理器的 prepareTrack 拿到曲目身分。
#include "output_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

#include <cstring>
#include <mutex>

namespace {

AudioFormat dsd64() {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

/// 測試用的 DSD 包裝器：輸出 176.4 kHz／24 bit 的 PCM（內容不重要）。
/// reason 空字串時當成 DoP（不改變位元流），「DSD 轉 PCM」時當成轉 PCM。
class TestDsdSink final : public auralis::DsdSink {
public:
    explicit TestDsdSink(QString reason) : m_reason(std::move(reason)) {}
    QString name() const override { return m_reason.isEmpty() ? QStringLiteral("DoP") : m_reason; }
    auralis::Result<AudioFormat> open(const auralis::DsdFormat& format) override {
        AudioFormat out;
        out.rate = 176400;
        out.bits = 24;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames * 2; }
    qint64 convert(const std::byte*, qint64 dsdFrames, std::byte* out) noexcept override {
        // 每個 DSD 樣本框變成兩個輸出樣本框，每框 2 聲道 × 4 位元組
        std::memset(out, 0, static_cast<std::size_t>(dsdFrames * 2 * 8));
        return dsdFrames * 2;
    }
    QString reason() const override { return m_reason; }

private:
    QString m_reason;
};

TrackRef addDsd(Fixture& f, const QString& name, double seconds) {
    const QString path = name + QStringLiteral(".mem");
    const AudioFormat format = dsd64();
    // 一個樣本框是每聲道 32 個 DSD 位元：每秒 2822400 ÷ 32 框
    const auto frames = static_cast<qsizetype>(format.dsdRate / 32 * seconds);
    f.sourceFactory->tracks[path] = {format, QByteArray(frames * format.bytesPerFrame(), '\x69')};
    return TrackRef{path, 0};
}

/// 記下每個處理器實例 prepareTrack() 拿到的曲目路徑（解碼執行緒呼叫）。
struct PreparedTracks {
    std::mutex mutex;
    QStringList paths;
};

class RecordingProcessor final : public auralis::IProcessor {
public:
    explicit RecordingProcessor(PreparedTracks& prepared) : m_prepared(prepared) {}
    void prepare(const AudioFormat&) override {}
    void prepareTrack(const AudioFormat&, const TrackRef& track) override {
        const std::lock_guard lock(m_prepared.mutex);
        m_prepared.paths << track.path;
    }
    bool isActive() const noexcept override { return false; }
    QString reason() const override { return {}; }
    void process(double*, qint64, int) noexcept override {}

private:
    PreparedTracks& m_prepared;
};

class RecordingFactory final : public auralis::IProcessorFactory {
public:
    explicit RecordingFactory(PreparedTracks& prepared) : m_prepared(prepared) {}
    QString name() const override { return QStringLiteral("記錄"); }
    std::unique_ptr<auralis::IProcessor> create() override {
        return std::make_unique<RecordingProcessor>(m_prepared);
    }

private:
    PreparedTracks& m_prepared;
};

} // namespace

TEST_CASE("AC-04-2（T-49）：DoP 包裝器遇到會改動訊號的輸出路徑（共享模式），核心不送出並跳過這首") {
    Fixture f;
    f.device("a").signalChange = QStringLiteral("共享模式");
    f.outputs.setDsdSink(std::make_shared<TestDsdSink>(QString()));
    const TrackRef a = addDsd(f, QStringLiteral("a"), 1.0);
    QSignalSpy skipped(&f.service, &PlaybackService::trackSkipped);
    f.service.playFrom({a}, 0);
    REQUIRE(skipped.size() == 1);
    CHECK(skipped.at(0).at(1).toString().contains(QStringLiteral("共享模式")));
    CHECK(f.service.state() == State::Stopped);
    // 裝置沒有開過：沒有任何 DoP 封包送進共享模式
    REQUIRE(f.factory->last != nullptr);
    CHECK(f.factory->last->capture().captured().isEmpty());
}

TEST_CASE(
    "AC-04-2（T-49）：播放中換輸出路徑前先發 outputPathChanging，收到時換的包裝器用在新輸出") {
    Fixture f;
    f.device("b").signalChange = QStringLiteral("共享模式");
    f.outputs.setDsdSink(std::make_shared<TestDsdSink>(QString()));
    const TrackRef a = addDsd(f, QStringLiteral("a"), 6.0);
    QStringList changing;
    QObject::connect(&f.service, &PlaybackService::outputPathChanging, [&](const QString& backend) {
        changing << backend;
        // 模組在這裡依新的輸出路徑換包裝器（共享模式不能 DoP，改轉 PCM）
        if (changing.size() > 1) {
            f.outputs.setDsdSink(std::make_shared<TestDsdSink>(QStringLiteral("DSD 轉 PCM")));
        }
    });
    QSignalSpy skipped(&f.service, &PlaybackService::trackSkipped);
    f.service.playFrom({a}, 0);
    // 第一次播放選預設輸出時也發
    CHECK(changing == QStringList{QStringLiteral("測試路徑")});
    REQUIRE(f.drive([&] { return f.service.positionMs() > 500; }));
    const qint64 at = f.service.positionMs();

    REQUIRE(f.service.switchOutput(f.choice("b")));
    CHECK(changing.size() == 2);
    CHECK(skipped.isEmpty());
    CHECK(f.service.state() == State::Playing);
    CHECK(f.service.positionMs() == at);
    CHECK(f.service.outputStatus().reasons.contains(QStringLiteral("DSD 轉 PCM")));
}

TEST_CASE("T-49：處理器的 prepareTrack 拿到這一首的身分，無縫接續時是接續的那一首") {
    Fixture f;
    PreparedTracks prepared;
    f.processors.add(std::make_shared<RecordingFactory>(prepared));
    const TrackRef a = f.add("a", 1.0);
    const TrackRef b = f.add("b", 1.0);
    f.service.playFrom({a, b}, 0);
    REQUIRE(f.drive([&] { return f.service.state() == State::Stopped; }));
    const std::lock_guard lock(prepared.mutex);
    CHECK(prepared.paths == QStringList{a.path, b.path});
}
