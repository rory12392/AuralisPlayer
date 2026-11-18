// ReplayGain（T-37）：模式與設定、增益來源的優先順序、防削波上限、部分專輯退回音軌增益並說明、
// DSD 時不作用、處理器的作用中與原因（AC-06-1、2、4，規格 §1 情況 4）。
#include "fake_playback_service.h"
#include "replay_gain.h"
#include "replay_gain_controller.h"
#include "replay_gain_processor.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>
#include <auralis/module_settings.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using auralis::AudioFormat;
using auralis::OutputStatus;
using auralis::TrackRef;
using auralis::mod::out::RgAppliedLog;
using auralis::mod::out::RgMode;
using auralis::mod::out::RgSettings;
using auralis::mod::out::RgSource;
using auralis::mod::out::RgTrackGain;
using auralis::mod::out::RgValues;
using auralis::test::FakePlaybackService;
using namespace auralis::mod::out;

namespace {

void ensureApp() {
    static int argc = 1;
    static char name[] = "auralis_out_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QCoreApplication(argc, argv);
    }
}

class MemorySettings final : public auralis::ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { values[key] = value; }
    bool contains(const QString& key) const override { return values.contains(key); }
    void remove(const QString& key) override { values.remove(key); }
    QVariantMap values;
};

const AudioFormat kPcm{44100, 24, 32, 2, false, 0};
const TrackRef kTrack{QStringLiteral("D:\\音樂\\專輯\\01.flac"), 0};

/// 固定回傳 gain 的查法。
RgLookup fixed(const RgValues& computed, const RgValues& tags) {
    return [computed, tags](const TrackRef&) {
        return chooseGain(computed, tags);
    };
}

/// 處理一段固定振幅的訊號，回傳處理後的最大絕對值（處理器不作用時引擎不呼叫 process，照樣回原值）。
double processedPeak(ReplayGainProcessor& processor, double amplitude) {
    std::vector<double> samples(64);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = (i % 2 == 0 ? amplitude : -amplitude) * (i % 8 == 0 ? 1.0 : 0.5);
    }
    if (processor.isActive()) {
        processor.process(samples.data(), static_cast<qint64>(samples.size() / 2), 2);
    }
    double peak = 0.0;
    for (double sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

/// 假的 rg_computed（只回應 rg_computed 的查詢）。
class FakeDatabase final : public auralis::ModuleDatabase {
public:
    auralis::Result<qint64> execute(const QString&, const QVariantList&) override { return 0; }
    auralis::Result<QList<Row>> query(const QString& sql, const QVariantList& params) override {
        lastSql = sql;
        lastParams = params;
        return rows;
    }
    auralis::Result<bool> transaction(const std::function<bool()>& work) override { return work(); }
    QList<Row> rows;
    QString lastSql;
    QVariantList lastParams;
};

/// 假的音樂庫：findByIdentity 回傳設定好的曲目（含標籤的 ReplayGain）。
class FakeLibrary final : public auralis::LibraryQuery {
public:
    auralis::Result<std::optional<auralis::TrackInfo>> findByIdentity(const QString&,
                                                                      quint32) const override {
        return track;
    }
    auralis::Result<QList<auralis::TrackInfo>> tracks(const auralis::TrackFilter&) const override {
        return QList<auralis::TrackInfo>{};
    }
    auralis::Result<QList<auralis::AlbumInfo>> albums() const override {
        return QList<auralis::AlbumInfo>{};
    }
    auralis::Result<QStringList> artists() const override { return QStringList{}; }
    auralis::Result<QList<auralis::FolderInfo>> folders(qint64) const override {
        return QList<auralis::FolderInfo>{};
    }
    auralis::Result<QString> readEmbeddedLyrics(const TrackRef&) const override {
        return QString();
    }
    std::optional<auralis::TrackInfo> track;
};

} // namespace

TEST_CASE("AC-06-1：模式「關閉／音軌／專輯」預設關閉，選了寫進 OUT/replayGain；防削波預設開") {
    ensureApp();
    MemorySettings settings;
    FakePlaybackService playback;
    auto shared = std::make_shared<RgSettings>();
    ReplayGainController controller(settings, playback, shared, std::make_shared<RgAppliedLog>());
    CHECK(controller.modeIndex() == 0);
    CHECK(shared->mode.load() == RgMode::Off);
    CHECK(controller.preventClipping());
    REQUIRE(controller.options().size() == 3);
    CHECK(controller.options()[2].toMap().value(QStringLiteral("text")).toString() ==
          QStringLiteral("專輯"));

    controller.selectMode(2);
    CHECK(settings.values.value(QStringLiteral("replayGain")).toString() ==
          QStringLiteral("album"));
    CHECK(shared->mode.load() == RgMode::Album);
    controller.setPreventClipping(false);
    CHECK(settings.values.value(QStringLiteral("preventClipping")).toBool() == false);
    CHECK_FALSE(shared->preventClipping.load());

    // 重新啟動讀回記住的值；不認得的值當成關閉
    auto reread = std::make_shared<RgSettings>();
    ReplayGainController again(settings, playback, reread, nullptr);
    CHECK(again.modeIndex() == 2);
    CHECK_FALSE(again.preventClipping());
    CHECK(rgModeFromKey(QStringLiteral("loud")) == RgMode::Off);
}

TEST_CASE("AC-06-1：關閉時處理器不作用（引擎走整數直通，位元完美），樣本不變") {
    auto shared = std::make_shared<RgSettings>();
    ReplayGainProcessor processor(shared, fixed({}, {-6.0, 0.5, -8.0, 0.6}), nullptr);
    processor.prepareTrack(kPcm, kTrack);
    CHECK_FALSE(processor.isActive());
    CHECK(processor.currentFactor() == 1.0);
    std::vector<double> samples{0.25, -0.5, 0.125, 1.0};
    const auto before = samples;
    processor.process(samples.data(), 2, 2);
    CHECK(samples == before);
}

TEST_CASE("AC-06-2：音軌與專輯模式套用標籤的增益，播放中換模式立即生效") {
    auto shared = std::make_shared<RgSettings>();
    shared->preventClipping.store(false);
    ReplayGainProcessor processor(shared, fixed({}, {-6.0, 0.5, -8.0, 0.6}), nullptr);
    processor.prepareTrack(kPcm, kTrack);

    shared->mode.store(RgMode::Track);
    CHECK(processor.isActive());
    CHECK(processor.currentFactor() == Catch::Approx(std::pow(10.0, -6.0 / 20.0)));
    std::vector<double> samples{0.5, -0.5};
    processor.process(samples.data(), 1, 2);
    CHECK(samples[0] == Catch::Approx(0.5 * std::pow(10.0, -6.0 / 20.0)));
    CHECK(samples[1] == Catch::Approx(-0.5 * std::pow(10.0, -6.0 / 20.0)));

    shared->mode.store(RgMode::Album);
    CHECK(processor.currentFactor() == Catch::Approx(std::pow(10.0, -8.0 / 20.0)));
    shared->mode.store(RgMode::Off);
    CHECK_FALSE(processor.isActive());
}

TEST_CASE("來源優先順序：每個值都是 rg_computed 優先、其次標籤，峰值跟著增益的來源") {
    const RgValues computed{-3.0, 0.7, std::nullopt, std::nullopt};
    const RgValues tags{-6.0, 0.5, -8.0, 0.6};
    const RgTrackGain gain = chooseGain(computed, tags);
    CHECK(gain.track.gainDb == -3.0);
    CHECK(gain.track.peak == 0.7);
    CHECK(gain.track.source == RgSource::Computed);
    // 算過的只有音軌增益：專輯增益仍用標籤（不是退回音軌）
    CHECK(gain.album.gainDb == -8.0);
    CHECK(gain.album.peak == 0.6);
    CHECK(gain.album.source == RgSource::Tags);
    CHECK_FALSE(gain.album.albumFallback);

    const RgTrackGain none = chooseGain({}, {});
    CHECK_FALSE(none.track.gainDb.has_value());
    CHECK_FALSE(none.album.gainDb.has_value());
}

TEST_CASE("來源優先順序：正式的查法從 rg_computed 與音樂庫標籤取值") {
    FakeDatabase database;
    database.rows = {{-2.5, 0.8, QVariant(), QVariant()}};
    FakeLibrary library;
    auralis::TrackInfo info;
    info.rgTrackGain = -6.0;
    info.rgTrackPeak = 0.5;
    info.rgAlbumGain = -7.0;
    info.rgAlbumPeak = 0.9;
    library.track = info;
    const RgTrackGain gain = libraryLookup(database, library)(kTrack);
    CHECK(database.lastSql.contains(QStringLiteral("rg_computed")));
    CHECK(database.lastParams.value(0).toString() == auralis::pathKey(kTrack.path));
    CHECK(gain.track.gainDb == -2.5);
    CHECK(gain.track.source == RgSource::Computed);
    CHECK(gain.album.gainDb == -7.0);
    CHECK(gain.album.source == RgSource::Tags);

    // 兩邊都沒有：不調整
    database.rows.clear();
    library.track.reset();
    const RgTrackGain empty = libraryLookup(database, library)(kTrack);
    CHECK_FALSE(empty.track.gainDb.has_value());
}

TEST_CASE("AC-06-2：開啟防削波時增益不超過 -20·log10(peak)，套用後的峰值不超過 0 dBFS") {
    auto shared = std::make_shared<RgSettings>();
    shared->mode.store(RgMode::Track);
    // 增益 +6 dB、峰值 0.9：不防削波會放大到約 1.8
    ReplayGainProcessor processor(shared, fixed({}, {6.0, 0.9, std::nullopt, std::nullopt}),
                                  nullptr);
    processor.prepareTrack(kPcm, kTrack);
    shared->preventClipping.store(false);
    CHECK(processedPeak(processor, 0.9) > 1.0);
    shared->preventClipping.store(true);
    CHECK(processedPeak(processor, 0.9) <= 1.0 + 1e-12);
    CHECK(appliedGainDb(chooseGain({}, {6.0, 0.9, {}, {}}).track, true) ==
          Catch::Approx(-20.0 * std::log10(0.9)));

    // 負增益本來就不會削波，上限不影響
    CHECK(appliedGainDb(chooseGain({}, {-6.0, 0.9, {}, {}}).track, true) == -6.0);
    // 峰值不知道：只衰減不放大
    CHECK(appliedGainDb(chooseGain({}, {4.0, {}, {}, {}}).track, true) == 0.0);
    CHECK(appliedGainDb(chooseGain({}, {4.0, {}, {}, {}}).track, false) == 4.0);
}

TEST_CASE("情況 4：專輯模式但這首沒有專輯增益，退回音軌增益並在狀態說明") {
    // 同專輯只有部分曲目算過：這首 rg_computed 只有音軌增益，標籤也沒有專輯增益
    const RgTrackGain gain = chooseGain({-4.0, 0.5, std::nullopt, std::nullopt}, {});
    CHECK(gain.album.albumFallback);
    CHECK(gain.album.gainDb == -4.0);
    CHECK(gain.album.source == RgSource::Computed);

    auto shared = std::make_shared<RgSettings>();
    shared->mode.store(RgMode::Album);
    ReplayGainProcessor processor(shared, fixed({-4.0, 0.5, std::nullopt, std::nullopt}, {}),
                                  nullptr);
    processor.prepareTrack(kPcm, kTrack);
    CHECK(processor.isActive());
    CHECK(processor.currentFactor() == Catch::Approx(std::pow(10.0, -4.0 / 20.0)));

    CHECK(rgStatusText(RgMode::Album, true, {}, gain) ==
          QStringLiteral("這首沒有專輯增益，改用音軌增益 −4.0 dB（AuralisPlayer 計算）"));
    CHECK(rgStatusText(RgMode::Track, true, {}, gain) ==
          QStringLiteral("目前這首 −4.0 dB（音軌增益，AuralisPlayer 計算）"));

    // 區塊讀處理器替正在聽的這首記下的結果
    ensureApp();
    MemorySettings settings;
    settings.values[QStringLiteral("replayGain")] = QStringLiteral("album");
    FakePlaybackService playback;
    playback.playFrom({kTrack}, 0);
    auto log = std::make_shared<RgAppliedLog>();
    log->record(kTrack, gain);
    ReplayGainController controller(settings, playback, std::make_shared<RgSettings>(), log);
    CHECK(controller.status() ==
          QStringLiteral("這首沒有專輯增益，改用音軌增益 −4.0 dB（AuralisPlayer 計算）"));
    controller.selectMode(0);
    CHECK(controller.status().isEmpty());
}

TEST_CASE("狀態說明：沒有 ReplayGain 資訊、防削波降低、關閉時不寫") {
    const RgTrackGain none = chooseGain({}, {});
    CHECK(rgStatusText(RgMode::Track, true, {}, none) ==
          QStringLiteral("這首沒有 ReplayGain 資訊，未調整音量"));
    const RgTrackGain loud = chooseGain({}, {std::nullopt, std::nullopt, 3.0, 0.5});
    CHECK(rgStatusText(RgMode::Album, true, {}, loud) ==
          QStringLiteral("目前這首 +3.0 dB（專輯增益，標籤）"));
    const RgTrackGain clipped = chooseGain({}, {std::nullopt, std::nullopt, 9.0, 0.5});
    CHECK(rgStatusText(RgMode::Album, true, {}, clipped) ==
          QStringLiteral("目前這首 +6.0 dB（專輯增益，標籤）；防削波已從 +9.0 dB 降低"));
    CHECK(rgStatusText(RgMode::Off, true, {}, loud).isEmpty());
    CHECK(rgStatusText(RgMode::Track, true, {}, std::nullopt).isEmpty());
}

TEST_CASE("DSD：DoP、原生與轉 PCM 時 ReplayGain 不作用並在狀態說明") {
    const RgTrackGain gain = chooseGain({}, {-6.0, 0.5, -8.0, 0.6});
    CHECK(rgStatusText(RgMode::Track, true, QStringLiteral("DoP"), gain) ==
          QStringLiteral("DSD 直通時不作用"));
    CHECK(rgStatusText(RgMode::Album, true, QStringLiteral("原生"), gain) ==
          QStringLiteral("DSD 直通時不作用"));
    CHECK(rgStatusText(RgMode::Album, true, QStringLiteral("轉 PCM"), gain) ==
          QStringLiteral("DSD 轉 PCM 時不作用"));
    CHECK(rgStatusText(RgMode::Off, true, QStringLiteral("DoP"), gain).isEmpty());

    // 區塊依輸出狀態寫：DoP 播放中（裝置格式是 PCM、來源是 DSD）
    ensureApp();
    MemorySettings settings;
    settings.values[QStringLiteral("replayGain")] = QStringLiteral("track");
    FakePlaybackService playback;
    ReplayGainController controller(settings, playback, std::make_shared<RgSettings>(),
                                    std::make_shared<RgAppliedLog>());
    OutputStatus status;
    status.backend = QStringLiteral("WASAPI 獨占");
    status.sourceFormat = {0, 1, 32, 2, false, 2822400};
    status.deviceFormat = {176400, 24, 32, 2, false, 0};
    playback.setOutputStatus(status);
    CHECK(controller.status() == QStringLiteral("DSD 直通時不作用"));
}

TEST_CASE("AC-06-4：作用中時原因是「ReplayGain 已套用」；沒有增益資訊時不作用（位元完美）") {
    auto shared = std::make_shared<RgSettings>();
    shared->mode.store(RgMode::Track);
    auto log = std::make_shared<RgAppliedLog>();
    ReplayGainProcessorFactory factory(shared, fixed({}, {-6.0, 0.5, {}, {}}), log);
    CHECK(factory.name() == QStringLiteral("ReplayGain"));
    auto created = factory.create();
    auto* processor = dynamic_cast<ReplayGainProcessor*>(created.get());
    REQUIRE(processor != nullptr);

    // 只有 prepare(format)、不給曲目：不知道是哪一首，所以不作用（引擎呼叫的是 prepareTrack）
    processor->prepare(kPcm);
    CHECK_FALSE(processor->isActive());

    processor->prepareTrack(kPcm, kTrack);
    CHECK(processor->isActive());
    CHECK(processor->reason() == QStringLiteral("ReplayGain 已套用"));
    // 換曲時算的結果留給區塊說明（大小寫與分隔符不同也對得上）
    const auto recorded = log->find({QStringLiteral("d:/音樂/專輯/01.FLAC"), 0});
    REQUIRE(recorded.has_value());
    CHECK(recorded->track.gainDb == -6.0);

    ReplayGainProcessor bare(shared, fixed({}, {}), nullptr);
    bare.prepareTrack(kPcm, kTrack);
    CHECK_FALSE(bare.isActive());
}
