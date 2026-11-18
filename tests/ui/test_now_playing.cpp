// 正在播放面板（AC-10-6；畫面規格 S-11）與播放列、佇列列的封面：Player 的目前曲目資訊。
// 用假的 PlaybackService 與假的查詢，只確認畫面拿到的文字與封面網址。
#include "fake_playback_service.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"

#include <catch2/catch_test_macros.hpp>

#include <QAtomicInt>

#include <optional>

using auralis::AudioFormat;
using auralis::OutputStatus;
using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::test::FakePlaybackService;
using auralis::ui::nowPlayingFrom;
using auralis::ui::NowPlayingInfo;
using auralis::ui::PlayerController;
using auralis::ui::QueueModel;
using auralis::ui::QueueTrackMeta;

namespace {

const QString kCoverKey = QStringLiteral("e0123456789abcdef0123456789abcdef01234567");

/// 讀數的一格寫成「標籤=值 單位」，方便一次比對四格。
QStringList readoutTexts(const PlayerController& player) {
    QStringList texts;
    for (const QVariant& cell : player.readouts()) {
        const QVariantMap map = cell.toMap();
        const QString unit = map.value(QStringLiteral("unit")).toString();
        texts.append(map.value(QStringLiteral("label")).toString() + QLatin1Char('=') +
                     map.value(QStringLiteral("value")).toString() +
                     (unit.isEmpty() ? QString() : QLatin1Char(' ') + unit));
    }
    return texts;
}

NowPlayingInfo flacInfo() {
    NowPlayingInfo info;
    info.album = QStringLiteral("Goldberg Variations");
    info.year = 1981;
    info.trackNo = 3;
    info.trackTotal = 32;
    info.codec = QStringLiteral("FLAC");
    info.sampleRate = 96000;
    info.bits = 24;
    info.bitrateKbps = 2116;
    info.coverKey = kCoverKey;
    return info;
}

} // namespace

TEST_CASE("AC-10-6：正在播放面板顯示目前曲目的封面、專輯與年份、曲目序號與格式資訊") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    PlayerController player(service, model,
                            [](const TrackRef& track) -> std::optional<NowPlayingInfo> {
                                if (track.path.endsWith(QStringLiteral("1.flac"))) {
                                    return flacInfo();
                                }
                                return std::nullopt;
                            });
    service.reset(3, 1);
    player.waitForLookups();

    CHECK(player.coverUrl() == QStringLiteral("image://cover/") + kCoverKey);
    CHECK(player.album() == QStringLiteral("Goldberg Variations"));
    CHECK(player.albumLine() == QStringLiteral("Goldberg Variations　1981"));
    CHECK(player.trackText() == QStringLiteral("曲目 3 / 32"));
    CHECK(readoutTexts(player) ==
          QStringList{QStringLiteral("格式=FLAC"), QStringLiteral("取樣率=96 kHz"),
                      QStringLiteral("位元深度=24 bit"), QStringLiteral("位元率=2,116 kbps")});

    // 換到音樂庫沒有的曲目：舊的封面與資訊不留著，讀數寫「—」
    service.playAt(2);
    player.waitForLookups();
    CHECK(player.coverUrl().isEmpty());
    CHECK(player.albumLine().isEmpty());
    CHECK(player.trackText().isEmpty());
    CHECK(readoutTexts(player) == QStringList{QStringLiteral("格式=—"), QStringLiteral("取樣率=—"),
                                              QStringLiteral("位元深度=—"),
                                              QStringLiteral("位元率=—")});
}

TEST_CASE("AC-10-6：取樣率不是整數 kHz、有損格式沒有位元深度、沒有年份與總數時的寫法") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    PlayerController player(service, model, [](const TrackRef&) -> std::optional<NowPlayingInfo> {
        NowPlayingInfo info;
        info.album = QStringLiteral("Live");
        info.trackNo = 7;
        info.codec = QStringLiteral("MP3");
        info.sampleRate = 44100;
        info.bitrateKbps = 320;
        return info;
    });
    service.reset(1, 0);
    player.waitForLookups();

    CHECK(player.albumLine() == QStringLiteral("Live"));
    CHECK(player.trackText() == QStringLiteral("曲目 7"));
    CHECK(readoutTexts(player) ==
          QStringList{QStringLiteral("格式=MP3"), QStringLiteral("取樣率=44.1 kHz"),
                      QStringLiteral("位元深度=—"), QStringLiteral("位元率=320 kbps")});
}

TEST_CASE("AC-10-6：DSD 時讀數改成格式、DSD 倍率、輸出方式、位元率，輸出方式跟著輸出狀態") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    PlayerController player(service, model, [](const TrackRef&) -> std::optional<NowPlayingInfo> {
        NowPlayingInfo info;
        info.codec = QStringLiteral("DSD");
        info.sampleRate = 2822400;
        info.bits = 1;
        info.dsdRate = 2822400;
        info.bitrateKbps = 5645;
        return info;
    });
    service.reset(1, 0);
    player.waitForLookups();
    CHECK(readoutTexts(player) ==
          QStringList{QStringLiteral("格式=DSD"), QStringLiteral("DSD 倍率=DSD64"),
                      QStringLiteral("輸出方式=—"), QStringLiteral("位元率=5,645 kbps")});

    OutputStatus status;
    status.backend = QStringLiteral("WASAPI 獨占");
    status.sourceFormat = AudioFormat{0, 1, 32, 2, false, 2822400};
    status.deviceFormat = AudioFormat{176400, 24, 32, 2, false, 0};
    service.setOutputStatus(status);
    CHECK(readoutTexts(player).at(2) == QStringLiteral("輸出方式=DoP"));

    status.deviceFormat = AudioFormat{0, 1, 32, 2, false, 2822400};
    service.setOutputStatus(status);
    CHECK(readoutTexts(player).at(2) == QStringLiteral("輸出方式=原生 DSD"));

    status.deviceFormat = AudioFormat{352800, 24, 32, 2, false, 0};
    status.bitPerfect = false;
    status.reasons = {QStringLiteral("DSD 轉 PCM")};
    service.setOutputStatus(status);
    CHECK(readoutTexts(player).at(2) == QStringLiteral("輸出方式=轉 PCM"));
}

TEST_CASE("AC-10-6：同一首曲目不重查，沒有目前曲目時沒有資訊") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    QAtomicInt lookups;
    PlayerController player(service, model,
                            [&lookups](const TrackRef&) -> std::optional<NowPlayingInfo> {
                                lookups.fetchAndAddOrdered(1);
                                return flacInfo();
                            });
    CHECK(player.coverUrl().isEmpty());
    service.reset(3, 0);
    player.waitForLookups();
    // 佇列改變但目前曲目沒變：不重查
    service.enqueueLast({TrackRef{QStringLiteral("C:/t/9.flac"), 0}});
    player.waitForLookups();
    CHECK(lookups.loadAcquire() == 1);
    CHECK_FALSE(player.coverUrl().isEmpty());

    service.reset(0, -1);
    player.waitForLookups();
    CHECK(player.coverUrl().isEmpty());
    CHECK(player.albumLine().isEmpty());
}

TEST_CASE("AC-10-6：曲目資訊換成面板的資訊，位元率由檔案大小與長度算出") {
    TrackInfo track;
    track.album = QStringLiteral("A");
    track.year = 2001;
    track.trackNo = 2;
    track.codec = QStringLiteral("FLAC");
    track.sampleRate = 44100;
    track.bits = 16;
    track.durationMs = 200'000;
    track.coverKey = kCoverKey;

    // 35,280,000 位元組 × 8 ÷ 200 秒 ＝ 1,411.2 kbps
    const NowPlayingInfo info = nowPlayingFrom(track, 12, 35'280'000);
    CHECK(info.album == QStringLiteral("A"));
    CHECK(info.year == 2001);
    CHECK(info.trackNo == 2);
    CHECK(info.trackTotal == 12);
    CHECK(info.codec == QStringLiteral("FLAC"));
    CHECK(info.sampleRate == 44100);
    CHECK(info.bits == 16);
    CHECK(info.bitrateKbps == 1411);
    CHECK(info.coverKey == kCoverKey);

    // 檔案大小或長度不知道（CUE 曲目、讀不到）時位元率不知道
    CHECK(nowPlayingFrom(track, 12, -1).bitrateKbps == -1);
    track.durationMs = 0;
    CHECK(nowPlayingFrom(track, 12, 35'280'000).bitrateKbps == -1);
}

TEST_CASE("S-11：正在播放面板帶專輯 id 給跳頁用，不在音樂庫的專輯裡或換到查不到的曲目時是 0") {
    TrackInfo track;
    track.albumId = 42;
    CHECK(nowPlayingFrom(track, 0, -1).albumId == 42);

    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    PlayerController player(service, model,
                            [](const TrackRef& track) -> std::optional<NowPlayingInfo> {
                                if (track.path.endsWith(QStringLiteral("1.flac"))) {
                                    NowPlayingInfo info = flacInfo();
                                    info.albumId = 42;
                                    return info;
                                }
                                if (track.path.endsWith(QStringLiteral("0.flac"))) {
                                    // 在音樂庫裡、但不屬於任何專輯
                                    return NowPlayingInfo{};
                                }
                                return std::nullopt;
                            });
    CHECK(player.albumId() == 0);
    service.reset(3, 1);
    player.waitForLookups();
    CHECK(player.albumId() == 42);

    service.playAt(0);
    player.waitForLookups();
    CHECK(player.albumId() == 0);

    service.playAt(2);
    player.waitForLookups();
    CHECK(player.albumId() == 0);
}

TEST_CASE("AC-10-6：佇列列的封面縮圖用音樂庫查到的封面") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, [](const TrackRef& track) -> std::optional<QueueTrackMeta> {
        if (track.path.endsWith(QStringLiteral("0.flac"))) {
            return QueueTrackMeta{QStringLiteral("Aria"), QStringLiteral("Glenn Gould"), 185000,
                                  kCoverKey};
        }
        return std::nullopt;
    });
    service.reset(2, 0);
    model.waitForLookups();

    CHECK(model.roleNames().value(QueueModel::CoverUrlRole) == QByteArray("coverUrl"));
    CHECK(model.data(model.index(0), QueueModel::CoverUrlRole).toString() ==
          QStringLiteral("image://cover/") + kCoverKey);
    // 沒有封面時是空字串（共用元件 Cover 畫預設封面）
    CHECK(model.data(model.index(1), QueueModel::CoverUrlRole).toString().isEmpty());
}
