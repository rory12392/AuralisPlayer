// 單例 Playlists 的 C++ 部分（T-50）：加入曲目與通知（AC-11-4 的接收端）、拖到空位建立新清單、
// 刪除前確認與文案（AC-11-3，畫面規格 S-08）、只帶查詢條件的內容目前不接受。
#include "pl_test_helpers.h"
#include "playlist_store.h"
#include "playlists_controller.h"

#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/notifier.h>
#include <auralis/track_drag_payload.h>

#include <QCoreApplication>
#include <QThread>

#include <atomic>

using auralis::TrackDragPayload;
using auralis::mod::pl::PlaylistsController;
using auralis::mod::pl::PlaylistStore;
using auralis::test::PlDatabase;

namespace {

class FakeDialogs final : public auralis::Dialogs {
public:
    void chooseFolder(const QString&, PathsCallback) override {}
    void chooseFiles(const QString&, const QStringList&, PathsCallback) override {}
    void chooseSaveFile(const QString&, const QString&, const QStringList&,
                        PathsCallback) override {}
    void confirm(const auralis::ConfirmRequest& request, std::function<void(bool)> done) override {
        last = request;
        pending = std::move(done);
    }
    auralis::ConfirmRequest last;
    std::function<void(bool)> pending;
};

class FakeNotifier final : public auralis::Notifier {
public:
    void notify(const QString& text, Kind kind, const QString&, std::function<void()>) override {
        texts.append(text);
        kinds.append(kind);
    }
    QStringList texts;
    QList<Kind> kinds;
};

QString payload(int count) {
    TrackDragPayload p;
    for (int i = 1; i <= count; ++i) {
        p.tracks.append({QStringLiteral("D:/音樂/%1.flac").arg(i), 0});
    }
    return QString::fromUtf8(p.toJson());
}

/// 背景工作的結果排回主執行緒的事件迴圈，所以要有 QCoreApplication（整個測試程式一個）。
void ensureApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_pl_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
}

struct Fixture {
    PlDatabase db;
    PlaylistStore store{*db.db};
    FakeDialogs dialogs;
    FakeNotifier notifier;
    PlaylistsController controller{store, dialogs, notifier};
};

} // namespace

TEST_CASE("AC-11-1：新增播放清單用預設名稱並進入改名，改名後結束") {
    Fixture f;
    const qint64 id = f.controller.createPlaylist();
    REQUIRE(id > 0);
    CHECK(f.controller.editingId() == id);
    REQUIRE(f.controller.playlists().size() == 1);
    CHECK(f.controller.playlists().first().toMap().value("name").toString() ==
          QStringLiteral("新播放清單"));

    CHECK_FALSE(f.controller.rename(id, QStringLiteral("  ")));
    CHECK(f.controller.editingId() == id);
    CHECK(f.controller.rename(id, QStringLiteral("通勤")));
    CHECK(f.controller.editingId() == 0);
    CHECK(f.controller.info(id).value("name").toString() == QStringLiteral("通勤"));
}

TEST_CASE("AC-11-4：拖到播放清單＝加入最後，通知加了幾首到哪一個") {
    Fixture f;
    const qint64 id = f.controller.createPlaylist();
    REQUIRE(f.controller.rename(id, QStringLiteral("通勤")));
    CHECK(f.controller.addTracks(id, payload(3)));
    CHECK(f.controller.addTracks(id, payload(1)));
    CHECK(f.controller.info(id).value("count").toInt() == 4);
    REQUIRE(f.notifier.texts.size() == 2);
    CHECK(f.notifier.texts.first() == QStringLiteral("已加入 3 首到「通勤」"));
    CHECK(f.notifier.kinds.first() == auralis::Notifier::Kind::Info);
    // 同一首又加一次（情況 1）：兩筆都在
    const auto items = f.store.items(id).value();
    CHECK(items.at(0).track.path == items.at(3).track.path);

    // 不是曲目的內容、不存在的播放清單：不加
    CHECK_FALSE(f.controller.addTracks(id, QStringLiteral("{\"x\":1}")));
    CHECK_FALSE(f.controller.addTracks(id + 50, payload(1)));
}

TEST_CASE("AC-11-4：拖到空位＝建立新播放清單並加入，進入改名") {
    Fixture f;
    const qint64 id = f.controller.createWithTracks(payload(2));
    REQUIRE(id > 0);
    CHECK(f.controller.editingId() == id);
    CHECK(f.controller.info(id).value("count").toInt() == 2);
    // 沒有曲目的內容不建立空的清單
    CHECK(f.controller.createWithTracks(payload(0)) == 0);
    CHECK(f.controller.playlists().size() == 1);
}

TEST_CASE("AC-11-4：只帶查詢條件（超過 5,000 首）時在背景工作取曲目再加入") {
    ensureApp();
    PlDatabase db;
    PlaylistStore store(*db.db);
    FakeDialogs dialogs;
    FakeNotifier notifier;
    auralis::JobRunner jobs;
    QStringList asked;
    std::atomic<bool> onUiThread{false};
    const auto uiThread = QThread::currentThread();
    PlaylistsController controller(
        store, dialogs, notifier,
        [&](const QString& query) -> auralis::Result<QList<auralis::TrackInfo>> {
            asked.append(query);
            onUiThread = onUiThread || QThread::currentThread() == uiThread;
            if (query.contains(QStringLiteral("壞掉"))) {
                return auralis::Error{QStringLiteral("JSON 壞掉")};
            }
            QList<auralis::TrackInfo> found;
            for (int i = 0; i < 6000; ++i) {
                auralis::TrackInfo info;
                info.ref = {QStringLiteral("D:/音樂/%1.flac").arg(i), 0};
                found.append(info);
            }
            return found;
        },
        &jobs);
    const auto settle = [&] {
        REQUIRE(jobs.waitForDone(10000));
        QCoreApplication::processEvents();
    };
    const auto queryJson = [](const QString& query) {
        TrackDragPayload p;
        p.query = query;
        return QString::fromUtf8(p.toJson());
    };

    const qint64 id = controller.createPlaylist();
    REQUIRE(controller.rename(id, QStringLiteral("全部巴哈")));
    CHECK(controller.addTracks(id, queryJson(QStringLiteral("{\"lib\":1,\"text\":\"bach\"}"))));
    settle();
    CHECK(asked == QStringList{QStringLiteral("{\"lib\":1,\"text\":\"bach\"}")});
    CHECK_FALSE(onUiThread.load());
    CHECK(controller.info(id).value("count").toInt() == 6000);
    CHECK(notifier.texts.last() == QStringLiteral("已加入 6000 首到「全部巴哈」"));
    CHECK(store.items(id).value().at(5999).track.path == QStringLiteral("D:/音樂/5999.flac"));

    // 拖到空位：取回後才建立新清單並進入改名
    const QString byYear = queryJson(QStringLiteral("{\"lib\":1,\"year\":1720}"));
    CHECK(controller.createWithTracks(byYear) == -1);
    CHECK(controller.playlists().size() == 1);
    settle();
    REQUIRE(controller.playlists().size() == 2);
    CHECK(controller.playlists().last().toMap().value("count").toInt() == 6000);
    CHECK(controller.editingId() == controller.playlists().last().toMap().value("id").toLongLong());

    // 條件讀不懂：錯誤通知，不建立、不加入
    CHECK(controller.createWithTracks(queryJson(QStringLiteral("壞掉"))) == -1);
    settle();
    CHECK(controller.playlists().size() == 2);
    CHECK(notifier.texts.last() == QStringLiteral("無法取得曲目"));
    CHECK(notifier.kinds.last() == auralis::Notifier::Kind::Error);

    // 沒有給查詢的方法（測試替身）時不接受
    Fixture plain;
    const qint64 other = plain.controller.createPlaylist();
    CHECK_FALSE(plain.controller.addTracks(other, queryJson(QStringLiteral("{\"lib\":1}"))));
    CHECK(plain.controller.createWithTracks(queryJson(QStringLiteral("{\"lib\":1}"))) == 0);
}

TEST_CASE("AC-11-3：刪除前用確認對話框問，取消不刪、確認才刪") {
    Fixture f;
    const qint64 id = f.controller.createPlaylist();
    REQUIRE(f.controller.rename(id, QStringLiteral("通勤")));
    REQUIRE(f.controller.addTracks(id, payload(2)));
    qint64 removed = 0;
    QObject::connect(&f.controller, &PlaylistsController::playlistRemoved,
                     [&](qint64 value) { removed = value; });

    f.controller.requestRemove(id);
    REQUIRE(f.dialogs.pending);
    CHECK(f.dialogs.last.title == QStringLiteral("刪除播放清單「通勤」"));
    CHECK(f.dialogs.last.message == QStringLiteral("只刪除播放清單，不會刪除任何音樂檔。"));
    CHECK(f.dialogs.last.confirmText == QStringLiteral("刪除播放清單"));
    CHECK(f.dialogs.last.destructive);
    f.dialogs.pending(false);
    CHECK(f.controller.info(id).value("exists").toBool());
    CHECK(removed == 0);

    f.controller.requestRemove(id);
    f.dialogs.pending(true);
    CHECK_FALSE(f.controller.info(id).value("exists").toBool());
    CHECK(f.controller.playlists().isEmpty());
    CHECK(removed == id);
    CHECK(f.store.items(id).value().isEmpty());
}
