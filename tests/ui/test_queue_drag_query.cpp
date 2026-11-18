// T-62：佇列面板與 Player 的右鍵項目接受只帶查詢條件的 TrackDragPayload（超過 5,000 首，US-11
// AC-4）。 用假的 PlaybackService 與假的取曲目函式，確認曲目在工作執行緒取、取到後才動佇列、UI
// 執行緒不等。
#include "fake_playback_service.h"
#include "library/library_query_impl.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "ui/dialogs_impl.h"

#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>

#include <memory>

using auralis::Error;
using auralis::Result;
using auralis::TrackDragPayload;
using auralis::TrackInfo;
using auralis::test::FakePlaybackService;
using auralis::ui::PlayerController;
using auralis::ui::QueueModel;

namespace {

QString queryJson(const QString& query) {
    TrackDragPayload payload;
    payload.query = query;
    return QString::fromUtf8(payload.toJson());
}

/// 「q:<n>」→ C:/q/0.flac… n 首；其他的回傳錯誤。記下在哪條執行緒被呼叫。
struct FakeResolver {
    std::shared_ptr<QThread*> thread = std::make_shared<QThread*>(nullptr);

    Result<QList<TrackInfo>> operator()(const QString& query) const {
        *thread = QThread::currentThread();
        if (!query.startsWith(QStringLiteral("q:"))) {
            return Error{QStringLiteral("不是查詢條件")};
        }
        QList<TrackInfo> tracks(query.mid(2).toInt());
        for (qsizetype i = 0; i < tracks.size(); ++i) {
            tracks[i].ref = {QStringLiteral("C:/q/%1.flac").arg(i), 0};
        }
        return tracks;
    }
};

} // namespace

TEST_CASE("AC-11-4（T-62）：只帶查詢條件的拖放在工作執行緒取曲目，取到後放在指到的位置") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    FakeResolver resolver;
    model.setDragQueryResolver(resolver);

    SECTION("放在最後") {
        service.reset(3, 0);
        REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:6000")), model.count()));
        model.waitForLookups();
        CHECK(*resolver.thread != QThread::currentThread());
        CHECK(service.calls() == QStringList{QStringLiteral("enqueueLast 6000")});
        CHECK(service.paths().size() == 6003);
        CHECK(service.paths().last() == QStringLiteral("C:/q/5999.flac"));
    }
    SECTION("正在播放的下一列＝接著播放") {
        service.reset(4, 1);
        REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:6000")), 1));
        model.waitForLookups();
        CHECK(service.calls() == QStringList{QStringLiteral("enqueueNext 6000")});
    }
    SECTION("中間：移原本在後面的少數幾項，不是一首一首移 6,000 首") {
        service.reset(5, 1);
        REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:6000")), 2));
        model.waitForLookups();
        const QStringList paths = service.paths();
        REQUIRE(paths.size() == 6005);
        CHECK(paths.mid(0, 4) ==
              QStringList{"C:/t/0.flac", "C:/t/1.flac", "C:/t/2.flac", "C:/q/0.flac"});
        CHECK(paths.mid(6002) == QStringList{"C:/q/5999.flac", "C:/t/3.flac", "C:/t/4.flac"});
        CHECK(service.calls().size() == 3); // enqueueLast＋移兩項
    }
    SECTION("取到 0 首時佇列不動") {
        service.reset(2, 0);
        REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:0")), 1));
        model.waitForLookups();
        CHECK(service.calls().isEmpty());
    }
}

TEST_CASE("AC-11-4（T-62）：取曲目失敗時顯示錯誤通知、佇列不動；沒設取曲目函式時不接受") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    QueueModel model(service, &notifier, {});
    service.reset(2, 0);

    CHECK_FALSE(model.insertTracks(queryJson(QStringLiteral("q:6000")), 1));
    CHECK_FALSE(model.insertTracks(QStringLiteral("not json"), 1));

    model.setDragQueryResolver(FakeResolver{});
    REQUIRE(model.insertTracks(queryJson(QStringLiteral("壞掉")), 1));
    model.waitForLookups();
    CHECK(service.calls().isEmpty());
    CHECK(notifier.current().value("text").toString() ==
          QStringLiteral("無法取得曲目：不是查詢條件"));
}

TEST_CASE("AC-11-4（T-62）：右鍵的播放、接著播放、加到最後接受只帶查詢條件的內容") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    model.setDragQueryResolver(FakeResolver{});
    service.reset(3, 0);

    SECTION("播放") {
        auto player = std::make_unique<PlayerController>(service, model);
        REQUIRE(player->playTracks(queryJson(QStringLiteral("q:6000"))));
        model.waitForLookups();
        CHECK(service.calls() == QStringList{QStringLiteral("playFrom 0")});
        CHECK(service.paths().size() == 6000);
    }
    SECTION("接著播放、加到最後依序套用") {
        auto player = std::make_unique<PlayerController>(service, model);
        REQUIRE(player->enqueueNext(queryJson(QStringLiteral("q:6000"))));
        REQUIRE(player->enqueueLast(queryJson(QStringLiteral("q:5001"))));
        model.waitForLookups();
        CHECK(service.calls() ==
              QStringList{QStringLiteral("enqueueNext 6000"), QStringLiteral("enqueueLast 5001")});
    }
    SECTION("取到之前 Player 已經拆掉：不動佇列") {
        auto player = std::make_unique<PlayerController>(service, model);
        REQUIRE(player->enqueueLast(queryJson(QStringLiteral("q:6000"))));
        player.reset();
        model.waitForLookups();
        CHECK(service.calls().isEmpty());
    }
}

TEST_CASE("AC-11-4（T-62）：4 萬首的查詢條件放下時 UI 執行緒不等取曲目") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    service.reset(3, 0);
    // 取曲目卡住 1 秒（模擬讀資料庫）：放下的呼叫仍要馬上返回
    auto release = std::make_shared<QSemaphore>();
    model.setDragQueryResolver([release](const QString& query) {
        release->tryAcquire(1, 1000);
        return FakeResolver{}(query);
    });
    QElapsedTimer timer;
    timer.start();
    REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:40000")), model.count()));
    const qint64 dropMs = timer.elapsed();
    CHECK(dropMs < 100);
    CHECK(service.calls().isEmpty());
    release->release();
    model.waitForLookups();
    CHECK(service.calls() == QStringList{QStringLiteral("enqueueLast 40000")});
    WARN("4 萬首：放下的呼叫 " << dropMs << " ms，放下到加入 " << timer.elapsed()
                               << " ms（假的取曲目）");
}

TEST_CASE(
    "AC-11-4（M-04 審查）：取曲目期間佇列變了（刪除、重排、切歌），取到後放在原本那一項前面") {
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    auto release = std::make_shared<QSemaphore>();
    model.setDragQueryResolver([release](const QString& query) {
        release->tryAcquire(1, 5000);
        return FakeResolver{}(query);
    });
    // 放在第 row 列，取曲目卡住時做 meanwhile，放開後回傳佇列的路徑
    const auto drop = [&](int row, const std::function<void()>& meanwhile) {
        REQUIRE(model.insertTracks(queryJson(QStringLiteral("q:2")), row));
        meanwhile();
        release->release();
        model.waitForLookups();
        return service.paths();
    };
    const auto t = [](int n) {
        return QStringLiteral("C:/t/%1.flac").arg(n);
    };
    const QString q0 = QStringLiteral("C:/q/0.flac");
    const QString q1 = QStringLiteral("C:/q/1.flac");
    service.reset(4, 0); // 列：t0（正在播放）、t1、t2、t3

    SECTION("刪除前面的一項：仍放在 t2 前面") {
        CHECK(drop(2, [&] { model.removeAt(1); }) == QStringList{t(0), q0, q1, t(2), t(3)});
    }
    SECTION("指到的那一項被刪掉：退回加在最後") {
        CHECK(drop(2, [&] { model.removeAt(2); }) == QStringList{t(0), t(1), t(3), q0, q1});
    }
    SECTION("重排：t3 移到 t1 前面，仍放在 t2 前面") {
        CHECK(drop(2, [&] { model.moveTo(3, 1); }) == QStringList{t(0), t(3), t(1), q0, q1, t(2)});
    }
    SECTION("切歌：跳到 t2 播，仍放在 t3 前面（接著播放）") {
        CHECK(drop(3, [&] { model.playAt(2); }) == QStringList{t(0), t(1), t(2), q0, q1, t(3)});
    }
    SECTION("放在最後：期間加了一首，仍放在最後") {
        CHECK(drop(model.count(), [&] {
                  service.enqueueLast({{QStringLiteral("C:/x.flac"), 0}});
              }) == QStringList{t(0), t(1), t(2), t(3), QStringLiteral("C:/x.flac"), q0, q1});
    }
}

// 手動量測（隱藏，ctest 不跑）：AURALIS_PERF_DRAG_DB 指向已掃好 4 萬首的 library.db 複本，
// 用真的 LibraryQuery 量「放下 → 加入佇列」。T-62 量過一次，數字在 dev-log。
TEST_CASE("T-62 量測：4 萬首的查詢條件從放下到加入佇列", "[.perf]") {
    const QString databasePath = qEnvironmentVariable("AURALIS_PERF_DRAG_DB");
    if (databasePath.isEmpty()) {
        SKIP("沒設 AURALIS_PERF_DRAG_DB");
    }
    auralis::core::LibraryQueryImpl library(databasePath);
    FakePlaybackService service;
    QueueModel model(service, nullptr, {});
    model.setDragQueryResolver(
        [&library](const QString& query) { return library.tracksForDragQuery(query); });
    service.reset(3, 0);
    QElapsedTimer timer;
    timer.start();
    REQUIRE(model.insertTracks(queryJson(QStringLiteral(R"({"lib":1})")), model.count()));
    const qint64 dropMs = timer.elapsed();
    model.waitForLookups();
    const qint64 totalMs = timer.elapsed();
    REQUIRE(service.calls().size() == 1);
    WARN("真的音樂庫：" << service.paths().size() - 3 << " 首；放下的呼叫 " << dropMs
                        << " ms，放下到加入 " << totalMs << " ms");
}
