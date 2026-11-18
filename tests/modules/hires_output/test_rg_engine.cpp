// ReplayGain 經播放引擎（T-38 接線）：引擎把曲目身分交給處理器的 prepareTrack()，
// 無縫接續時下一首套用下一首的增益（T-37 回報列出的缺口，AC-06-2）。
#include "engine_helpers.h"
#include "replay_gain.h"
#include "replay_gain_processor.h"

#include "audio/next_track.h"
#include "audio/processor_chain_impl.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QHash>

#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>

using auralis::AudioFormat;
using auralis::TrackRef;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using namespace auralis::mod::out;
using namespace auralis::test;

namespace {

const AudioFormat kFormat{44100, 24, 32, 2, false, 0};
constexpr qint64 kFrames = 22050;
constexpr double kAmplitude = 0.25;

/// 每個樣本都是 kAmplitude 的 24-in-32 訊號。
QByteArray constantSignal() {
    QByteArray data(kFrames * kFormat.bytesPerFrame(), '\0');
    const auto sample = static_cast<qint32>(kAmplitude * 2147483648.0);
    for (qsizetype i = 0; i < data.size(); i += 4) {
        std::memcpy(data.data() + i, &sample, 4);
    }
    return data;
}

/// 依序交出帶曲目身分的曲目（佇列接續時核心也是這樣填 EngineTrack::track）。
class RefTrackList final : public auralis::core::NextTrackProvider {
public:
    void add(const TrackRef& ref, quint64 token) {
        EngineTrack track{std::make_unique<MemorySource>(kFormat, constantSignal()), 0, -1, token};
        track.track = ref;
        m_tracks.push_back(std::move(track));
    }
    std::optional<EngineTrack> nextTrack() override {
        if (m_tracks.empty()) {
            return std::nullopt;
        }
        EngineTrack track = std::move(m_tracks.front());
        m_tracks.pop_front();
        return track;
    }

private:
    std::deque<EngineTrack> m_tracks;
};

/// 第 frame 個樣本框左聲道的值（滿刻度 ±1.0）。
double sampleAt(const QByteArray& data, qint64 frame) {
    qint32 value = 0;
    std::memcpy(&value, data.constData() + frame * kFormat.bytesPerFrame(), 4);
    return value / 2147483648.0;
}

} // namespace

TEST_CASE("接線：無縫接續時下一首套用下一首的增益（音軌模式，經播放引擎）") {
    const TrackRef first{QStringLiteral("D:\\音樂\\A\\01.flac"), 0};
    const TrackRef second{QStringLiteral("D:\\音樂\\B\\01.flac"), 0};
    QHash<QString, double> gains{{first.path, -6.0}, {second.path, -12.0}};
    auto settings = std::make_shared<RgSettings>();
    settings->mode.store(RgMode::Track);
    settings->preventClipping.store(false);
    auto lookup = [gains](const TrackRef& track) {
        return chooseGain({}, {gains.value(track.path), 0.5, std::nullopt, std::nullopt});
    };
    ProcessorChainImpl chain;
    chain.add(std::make_shared<ReplayGainProcessorFactory>(settings, lookup, nullptr));
    PlaybackEngine engine(chain);
    RefTrackList next;
    next.add(second, 2);
    engine.setNextTrackProvider(&next);
    CaptureBackend capture(fastCapture());
    EngineTrack start{std::make_unique<MemorySource>(kFormat, constantSignal()), 0, -1, 1};
    start.track = first;
    REQUIRE(engine.play(std::move(start), capture).hasValue());
    REQUIRE(waitFinished(engine, 30000));

    // 同格式接續：一段裝置紀錄，前一半是第一首的增益、後一半是第二首的
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 1);
    const QByteArray& data = sessions[0].data;
    REQUIRE(data.size() >= 2 * kFrames * kFormat.bytesPerFrame());
    const double firstGain = kAmplitude * std::pow(10.0, -6.0 / 20.0);
    const double secondGain = kAmplitude * std::pow(10.0, -12.0 / 20.0);
    // 抖動只有最低位元，誤差遠小於 1e-4
    CHECK(sampleAt(data, 100) == Catch::Approx(firstGain).margin(1e-4));
    CHECK(sampleAt(data, kFrames - 100) == Catch::Approx(firstGain).margin(1e-4));
    CHECK(sampleAt(data, kFrames + 100) == Catch::Approx(secondGain).margin(1e-4));
    CHECK(sampleAt(data, 2 * kFrames - 100) == Catch::Approx(secondGain).margin(1e-4));
}
