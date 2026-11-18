// 佇列面板的清單模型與播放列的 Player（AC-11-2 的介面部分；畫面規格 S-02、S-13）。
// 用假的 PlaybackService，只確認畫面把操作轉成哪個服務呼叫、列出的內容對不對。
#include "fake_playback_service.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "ui/dialogs_impl.h"

#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

#include <optional>

using auralis::TrackDragPayload;
using auralis::TrackRef;
using auralis::test::FakePlaybackService;
using auralis::ui::PlayerController;
using auralis::ui::QueueModel;
using auralis::ui::QueueTrackMeta;

namespace {

QString dragJson(const QStringList& paths) {
    TrackDragPayload payload;
    for (const auto& path : paths) {
        payload.tracks.append({path, 0});
    }
    return QString::fromUtf8(payload.toJson());
}

QString titleAt(const QueueModel& model, int row) {
    return model.data(model.index(row), QueueModel::TitleRole).toString();
}

} // namespace

TEST_CASE("AC-11-2：佇列面板從目前曲目列出，第一列是「正在播放」") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    service.reset(5, 2);
    REQUIRE(model.count() == 3);
    CHECK(model.hasCurrent());
    CHECK(model.data(model.index(0), QueueModel::CurrentRole).toBool());
    CHECK_FALSE(model.data(model.index(1), QueueModel::CurrentRole).toBool());
    // 還沒查到曲目資訊時用不含副檔名的檔名
    CHECK(titleAt(model, 0) == QStringLiteral("2"));
    CHECK(titleAt(model, 2) == QStringLiteral("4"));

    service.reset(3, -1);
    CHECK(model.count() == 3);
    CHECK_FALSE(model.hasCurrent());
}

TEST_CASE("AC-11-2：雙擊、移出、排序對應到佇列的位置") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    service.reset(6, 2);

    model.playAt(1);
    CHECK(service.calls().last() == QStringLiteral("playAt 3"));

    service.reset(6, 2);
    model.removeAt(2);
    CHECK(service.calls().last() == QStringLiteral("remove 4"));

    service.reset(6, 2);
    model.moveTo(3, 1);
    CHECK(service.calls().last() == QStringLiteral("move 5 3"));
    // 「正在播放」那列不動，其他列也不能移到它前面
    model.moveTo(0, 2);
    model.moveTo(2, 0);
    CHECK(service.calls().last() == QStringLiteral("move 4 3"));
    CHECK(service.calls().size() == 2);
}

TEST_CASE("AC-11-2：拖來的曲目放在指到的位置") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});

    SECTION("正在播放的下一列＝接著播放") {
        service.reset(4, 1);
        REQUIRE(model.insertTracks(dragJson({"C:/n/a.flac", "C:/n/b.flac"}), 1));
        CHECK(service.calls() == QStringList{QStringLiteral("enqueueNext 2")});
        CHECK(service.paths().mid(2, 2) == QStringList{"C:/n/a.flac", "C:/n/b.flac"});
    }
    SECTION("中間的位置") {
        service.reset(5, 1);
        REQUIRE(model.insertTracks(dragJson({"C:/n/a.flac", "C:/n/b.flac"}), 2));
        CHECK(service.paths() == QStringList{"C:/t/0.flac", "C:/t/1.flac", "C:/t/2.flac",
                                             "C:/n/a.flac", "C:/n/b.flac", "C:/t/3.flac",
                                             "C:/t/4.flac"});
    }
    SECTION("最後") {
        service.reset(3, 0);
        REQUIRE(model.insertTracks(dragJson({"C:/n/a.flac"}), model.count()));
        CHECK(service.calls() == QStringList{QStringLiteral("enqueueLast 1")});
        CHECK(service.paths().last() == QStringLiteral("C:/n/a.flac"));
    }
    SECTION("不能放到正在播放前面") {
        service.reset(3, 1);
        REQUIRE(model.insertTracks(dragJson({"C:/n/a.flac"}), 0));
        CHECK(service.paths().at(2) == QStringLiteral("C:/n/a.flac"));
    }
    SECTION("沒有目前曲目時可以放在最前面") {
        service.reset(2, -1);
        REQUIRE(model.insertTracks(dragJson({"C:/n/a.flac"}), 0));
        CHECK(service.paths().first() == QStringLiteral("C:/n/a.flac"));
    }
    SECTION("只帶查詢條件或不是曲目的內容不接受") {
        service.reset(2, 0);
        TrackDragPayload queryOnly;
        queryOnly.query = QStringLiteral("all");
        CHECK_FALSE(model.insertTracks(QString::fromUtf8(queryOnly.toJson()), 1));
        CHECK_FALSE(model.insertTracks(QStringLiteral("not json"), 1));
        CHECK(service.calls().isEmpty());
    }
}

TEST_CASE("AC-11-2：清空佇列留下正在播的那首，復原把接下來的曲目照順序加回") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    QueueModel model(service, &notifier, {});
    service.reset(5, 1);

    model.clearQueue();
    REQUIRE(service.paths() == QStringList{"C:/t/1.flac"});
    const QVariantMap notice = notifier.current();
    CHECK(notice.value("text").toString() == QStringLiteral("已清空佇列"));
    CHECK(notice.value("actionText").toString() == QStringLiteral("復原"));

    notifier.triggerAction(notice.value("id").toInt());
    CHECK(service.paths() ==
          QStringList{"C:/t/1.flac", "C:/t/2.flac", "C:/t/3.flac", "C:/t/4.flac"});
}

TEST_CASE("AC-11-2：曲名、藝人、長度在背景向音樂庫查，查到後更新列與播放列") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, [](const TrackRef& track) -> std::optional<QueueTrackMeta> {
        if (track.path.endsWith(QStringLiteral("1.flac"))) {
            return QueueTrackMeta{QStringLiteral("Aria"), QStringLiteral("Glenn Gould"), 185000};
        }
        return std::nullopt;
    });
    PlayerController player(service, model);
    QSignalSpy trackSpy(&player, &PlayerController::trackChanged);
    service.reset(3, 1);
    model.waitForLookups();

    CHECK(titleAt(model, 0) == QStringLiteral("Aria"));
    CHECK(model.data(model.index(0), QueueModel::ArtistRole).toString() ==
          QStringLiteral("Glenn Gould"));
    // 音樂庫沒有的曲目照樣用檔名
    CHECK(titleAt(model, 1) == QStringLiteral("2"));
    CHECK(player.title() == QStringLiteral("Aria"));
    CHECK(player.artist() == QStringLiteral("Glenn Gould"));
    CHECK(trackSpy.count() >= 2);
}

TEST_CASE("AC-11-2：播放列的重複循環切換、播放鍵與時間寫法") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    PlayerController player(service, model);
    service.reset(2, -1);

    CHECK(player.repeatMode() == QStringLiteral("off"));
    player.cycleRepeat();
    CHECK(player.repeatMode() == QStringLiteral("all"));
    player.cycleRepeat();
    CHECK(player.repeatMode() == QStringLiteral("one"));
    player.cycleRepeat();
    CHECK(player.repeatMode() == QStringLiteral("off"));

    player.toggleShuffle();
    CHECK(player.shuffle());

    // 停止時按播放從佇列開始播；播放中再按是暫停
    CHECK_FALSE(player.hasTrack());
    CHECK(player.canPlay());
    player.togglePause();
    CHECK(player.playing());
    CHECK(player.hasTrack());
    player.togglePause();
    CHECK_FALSE(player.playing());

    CHECK(player.formatTime(-1).isEmpty());
    CHECK(player.formatTime(0) == QStringLiteral("0:00"));
    CHECK(player.formatTime(65'400) == QStringLiteral("1:05"));
    CHECK(player.formatTime(3'725'000) == QStringLiteral("1:02:05"));
}
