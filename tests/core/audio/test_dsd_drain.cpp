// 有濾波狀態的 DSD 包裝器（轉 PCM）在換格式時要把曲目最後一段送完（AC-04-5、AC-04-6）：
// 排空前引擎先把靜音碼 0x69 經包裝器寫入，濾波器裡還沒輸出的那一段被推出來，
// 不會被 render 補的靜音取代。用「延遲 N 框」的假包裝器代表濾波器的群延遲，逐位元組比對。
#include "engine_helpers.h"
#include "signal_writer.h"

#include <auralis/dsd.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::Result;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::NextTrackProvider;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::cd;
using auralis::test::fastCapture;
using auralis::test::MemorySource;

namespace {

constexpr quint8 kDsdSilence = 0x69;

/// 假的轉 PCM 包裝器：每個 DSD 樣本框原封不動變成一個 32 位元的輸出框，但晚 kDelay 框才出來
/// （和 FIR 的群延遲一樣，最後 kDelay 框要再餵東西進去才推得出來）。延遲線開始時填滿靜音碼。
class DelaySink final : public DsdSink {
public:
    static constexpr qint64 kDelay = 16;

    QString name() const override { return QStringLiteral("延遲（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
        m_frameBytes = format.bytesPerFrame();
        m_history.assign(static_cast<std::size_t>(kDelay * m_frameBytes), std::byte{kDsdSilence});
        m_at = 0;
        AudioFormat out;
        out.rate = format.dsdRate / 32;
        out.bits = 32;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        const auto frameBytes = static_cast<std::size_t>(m_frameBytes);
        for (qint64 f = 0; f < dsdFrames; ++f) {
            std::byte* slot = m_history.data() + m_at * frameBytes;
            std::memcpy(out + f * m_frameBytes, slot, frameBytes);
            std::memcpy(slot, dsd + f * m_frameBytes, frameBytes);
            m_at = (m_at + 1) % static_cast<std::size_t>(kDelay);
        }
        return dsdFrames;
    }
    QString reason() const override { return QStringLiteral("DSD 轉 PCM"); }

private:
    int m_frameBytes = 0;
    std::vector<std::byte> m_history;
    std::size_t m_at = 0;
};

AudioFormat dsdFormat(quint32 dsdRate) {
    AudioFormat format;
    format.dsdRate = dsdRate;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

/// 隨機的 DSD 位元流，刻意不含靜音碼 0x69，才分得出曲目和引擎寫的靜音。
QByteArray dsdNoise(qint64 frames, unsigned seed) {
    QByteArray data(static_cast<qsizetype>(frames * 8), Qt::Uninitialized);
    std::mt19937 random(seed);
    for (char& byte : data) {
        const auto value = static_cast<quint8>(random() & 0xFF);
        byte = static_cast<char>(value == kDsdSilence ? 0x96 : value);
    }
    return data;
}

class OneNext final : public NextTrackProvider {
public:
    explicit OneNext(EngineTrack track) { m_track.push_back(std::move(track)); }
    std::optional<EngineTrack> nextTrack() override {
        if (m_track.empty()) {
            return std::nullopt;
        }
        EngineTrack track = std::move(m_track.front());
        m_track.pop_front();
        return track;
    }

private:
    std::deque<EngineTrack> m_track;
};

/// 播 DSD 曲目 a，接著換到 next；回傳第一次開裝置（DSD 那段）送進裝置的內容。
QByteArray firstSession(const QByteArray& a, quint32 dsdRate, EngineTrack next) {
    OneNext provider(std::move(next));
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<DelaySink>());
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    const auto played = engine.play(
        EngineTrack{std::make_unique<MemorySource>(dsdFormat(dsdRate), a), 0, -1, 1}, capture);
    REQUIRE(played.hasValue());
    REQUIRE(auralis::test::waitFinished(engine));
    CHECK_FALSE(engine.decodeError().has_value());
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 2);
    return sessions[0].data;
}

} // namespace

TEST_CASE("AC-04-6：從 DSD 換到 PCM，有濾波狀態的包裝器把曲目最後一段送完才補靜音") {
    const QByteArray a = dsdNoise(8000, 491);
    const QByteArray b = auralis::test::makeNoise(cd(), 4410, 492);
    const QByteArray dsd =
        firstSession(a, 2822400, EngineTrack{std::make_unique<MemorySource>(cd(), b), 0, -1, 2});

    // 曲目完整出現（延遲線裡的最後 kDelay 框也在），之後全是靜音碼
    const qsizetype at = dsd.indexOf(a);
    INFO("曲目在裝置資料裡的位置：" << at);
    REQUIRE(at >= 0);
    const QByteArray tail = dsd.mid(at + a.size());
    CHECK(std::all_of(tail.begin(), tail.end(),
                      [](char byte) { return static_cast<quint8>(byte) == kDsdSilence; }));
}

TEST_CASE("AC-04-6：DSD 倍率改變（DSD64 → DSD128）時，曲目最後一段也送完才換") {
    const QByteArray a = dsdNoise(8000, 493);
    const QByteArray b = dsdNoise(8000, 494);
    const QByteArray dsd = firstSession(
        a, 2822400, EngineTrack{std::make_unique<MemorySource>(dsdFormat(5644800), b), 0, -1, 2});
    CHECK(dsd.indexOf(a) >= 0);
}
