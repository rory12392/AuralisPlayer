// 播放清單頁的曲目清單（單例 PlaylistTracks，T-55）：顯示資訊與找不到檔案、從某一首播放、
// 拖曳排序與鍵盤排序（放開時一次寫入、重新啟動後保留）、移除與復原、插在拖放位置、同一首兩次。
#include "pl_test_helpers.h"
#include "playlist_store.h"
#include "playlist_tracks.h"
#include "playlists_controller.h"

#include "fake_playback_service.h"

#include <auralis/dialogs.h>
#include <auralis/library_query.h>
#include <auralis/notifier.h>
#include <auralis/track_drag_payload.h>

#include <QCoreApplication>
#include <QElapsedTimer>

using auralis::TrackDragPayload;
using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::mod::pl::PlaylistsController;
using auralis::mod::pl::PlaylistStore;
using auralis::mod::pl::PlaylistTracks;
using auralis::test::PlDatabase;

namespace {

class NoDialogs final : public auralis::Dialogs {
public:
    void chooseFolder(const QString&, PathsCallback) override {}
    void chooseFiles(const QString&, const QStringList&, PathsCallback) override {}
    void chooseSaveFile(const QString&, const QString&, const QStringList&,
                        PathsCallback) override {}
    void confirm(const auralis::ConfirmRequest&, std::function<void(bool)>) override {}
};

class ActionNotifier final : public auralis::Notifier {
public:
    void notify(const QString& text, Kind, const QString& actionText,
                std::function<void()> callback) override {
        texts.append(text);
        lastAction = actionText;
        action = std::move(callback);
    }
    QStringList texts;
    QString lastAction;
    std::function<void()> action;
};

/// 音樂庫裡只有路徑在 known 裡的曲目（曲名是路徑最後一段加「曲」，長度 1 分鐘）。
class KnownLibrary final : public auralis::LibraryQuery {
public:
    QStringList known;
    /// true 時曲目是 DSD64
    bool dsd = false;
    auralis::Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                             quint32 subsong) const override {
        if (!known.contains(path)) {
            return std::optional<TrackInfo>();
        }
        TrackInfo t;
        t.ref = {path, subsong};
        t.title = path.section(QLatin1Char('/'), -1) + QStringLiteral("曲");
        t.artist = QStringLiteral("藝人");
        t.codec = dsd ? QStringLiteral("DSD") : QStringLiteral("FLAC");
        t.sampleRate = dsd ? 2822400 : 96000;
        t.dsdRate = dsd ? 2822400 : 0;
        t.bits = dsd ? 1 : 24;
        t.durationMs = 60'000;
        return std::optional<TrackInfo>(t);
    }
    auralis::Result<QList<TrackInfo>> tracks(const auralis::TrackFilter&) const override {
        return QList<TrackInfo>();
    }
    auralis::Result<QList<auralis::AlbumInfo>> albums() const override {
        return QList<auralis::AlbumInfo>();
    }
    auralis::Result<QStringList> artists() const override { return QStringList(); }
    auralis::Result<QList<auralis::FolderInfo>> folders(qint64) const override {
        return QList<auralis::FolderInfo>();
    }
    auralis::Result<QString> readEmbeddedLyrics(const TrackRef&) const override {
        return QString();
    }
};

void ensureApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_pl_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
}

TrackRef ref(const QString& name) {
    return {QStringLiteral("D:/音樂/") + name, 0};
}

struct Fixture {
    PlDatabase db;
    std::unique_ptr<PlaylistStore> store = std::make_unique<PlaylistStore>(*db.db);
    NoDialogs dialogs;
    ActionNotifier notifier;
    KnownLibrary library;
    auralis::test::FakePlaybackService playback;
    std::unique_ptr<PlaylistsController> controller;
    std::unique_ptr<PlaylistTracks> tracks;
    qint64 id = 0;

    /// 播放清單裡依序放 names（同名就是同一首）。
    explicit Fixture(const QStringList& names) {
        ensureApp();
        id = store->create(QStringLiteral("通勤")).value();
        QList<TrackRef> refs;
        for (const auto& name : names) {
            refs.append(ref(name));
        }
        REQUIRE(store->addTracks(id, refs).value());
        open();
    }
    /// 建立控制器與清單並載入（重新啟動時也用）。
    void open() {
        tracks.reset();
        controller = std::make_unique<PlaylistsController>(*store, dialogs, notifier, nullptr,
                                                           nullptr, nullptr, &playback);
        tracks = std::make_unique<PlaylistTracks>(*store, *controller, &library, &playback);
        tracks->setPlaylistId(id);
        waitLoaded();
    }
    void waitLoaded() {
        QElapsedTimer timer;
        timer.start();
        while (tracks->status() != QStringLiteral("ready") && timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
        }
        REQUIRE(tracks->status() == QStringLiteral("ready"));
    }
    /// 資料庫裡的順序（路徑最後一段）。
    QStringList stored() const {
        QStringList names;
        // 先存起來：range-for 不會延長 items() 暫存結果的生命週期
        const auto items = store->items(id);
        for (const auto& item : items.value()) {
            names.append(item.track.path.section(QLatin1Char('/'), -1));
        }
        return names;
    }
    /// 清單上的順序（路徑最後一段）。
    QStringList shown() const {
        QStringList names;
        for (int i = 0; i < tracks->rowCount(); ++i) {
            names.append(tracks->data(tracks->index(i), PlaylistTracks::PathRole)
                             .toString()
                             .section(QLatin1Char('/'), -1));
        }
        return names;
    }
    QVariant role(int row, int role) const { return tracks->data(tracks->index(row), role); }
};

} // namespace

TEST_CASE("S-08：顯示音樂庫的曲目資訊；找不到的用檔名並標 missing，總長只算找得到的") {
    Fixture f({QStringLiteral("a.flac"), QStringLiteral("b.flac")});
    REQUIRE(f.role(0, PlaylistTracks::MissingRole).toBool());
    // 音樂庫掃描完：清掉查過的資訊重讀
    f.library.known = {ref(QStringLiteral("a.flac")).path};
    emit f.library.scanFinished();
    QElapsedTimer timer;
    timer.start();
    while (f.role(0, PlaylistTracks::MissingRole).toBool() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    REQUIRE(f.tracks->count() == 2);
    CHECK(f.role(0, PlaylistTracks::TitleRole).toString() == QStringLiteral("a.flac曲"));
    CHECK(f.role(0, PlaylistTracks::QualityTextRole).toString() ==
          QStringLiteral("96 kHz / 24 bit"));
    CHECK(f.role(0, PlaylistTracks::DurationTextRole).toString() == QStringLiteral("1:00"));
    CHECK_FALSE(f.role(0, PlaylistTracks::MissingRole).toBool());
    CHECK(f.role(1, PlaylistTracks::TitleRole).toString() == QStringLiteral("b"));
    CHECK(f.role(1, PlaylistTracks::MissingRole).toBool());
    CHECK(f.tracks->totalDurationText() == QStringLiteral("1:00"));
}

TEST_CASE("S-08：DSD 曲目的格式與取樣率寫 DSD64（介面規範 §6.1）") {
    Fixture f({QStringLiteral("d.dsf")});
    f.library.known = {ref(QStringLiteral("d.dsf")).path};
    f.library.dsd = true;
    emit f.library.scanFinished();
    QElapsedTimer timer;
    timer.start();
    while (f.role(0, PlaylistTracks::MissingRole).toBool() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    CHECK(f.role(0, PlaylistTracks::FormatRole).toString() == QStringLiteral("DSD64"));
    CHECK(f.role(0, PlaylistTracks::QualityTextRole).toString() == QStringLiteral("DSD64"));
}

TEST_CASE("AC-11-1：拖曳排序放開時一次寫入，重新啟動後保留") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    // 把 c、d 一起拖到 a 前面
    CHECK(f.tracks->moveRows({2, 3}, 0) == 0);
    CHECK(f.shown() == QStringList{"c", "d", "a", "b"});
    CHECK(f.stored() == QStringList{"c", "d", "a", "b"});
    // 把 c 拖到最後（target＝列數）
    CHECK(f.tracks->moveRows({0}, 4) == 3);
    CHECK(f.shown() == QStringList{"d", "a", "b", "c"});
    // 放回原位置不寫入
    CHECK(f.tracks->moveRows({1}, 1) == -1);
    f.db.reopen();
    f.store = std::make_unique<PlaylistStore>(*f.db.db);
    f.open();
    CHECK(f.shown() == QStringList{"d", "a", "b", "c"});
}

TEST_CASE("AC-11-1：鍵盤排序：選取的列往上、往下移一格，頂到邊的不動，選取跟著走") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    f.tracks->select(2);
    CHECK(f.tracks->moveSelectionBy(-1));
    CHECK(f.stored() == QStringList{"a", "c", "b", "d"});
    CHECK(f.tracks->isSelected(1));
    CHECK(f.tracks->selectedCount() == 1);
    f.tracks->select(0);
    CHECK_FALSE(f.tracks->moveSelectionBy(-1));
    f.tracks->toggle(3);
    CHECK(f.tracks->moveSelectionBy(1));
    CHECK(f.stored() == QStringList{"c", "a", "b", "d"});
}

TEST_CASE("S-08：Delete 移除選取的列，通知「已移除 <n> 首」，「復原」放回原位置") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"),
               QStringLiteral("e")});
    f.tracks->select(1);
    f.tracks->toggle(2);
    f.tracks->toggle(4);
    REQUIRE(f.tracks->removeSelected());
    CHECK(f.shown() == QStringList{"a", "d"});
    CHECK(f.stored() == QStringList{"a", "d"});
    CHECK(f.notifier.texts.last() == QStringLiteral("已移除 3 首"));
    CHECK(f.notifier.lastAction == QStringLiteral("復原"));
    REQUIRE(f.notifier.action);
    f.notifier.action();
    CHECK(f.stored() == QStringList{"a", "b", "c", "d", "e"});
    f.waitLoaded();
    QElapsedTimer timer;
    timer.start();
    while (f.tracks->count() != 5 && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    CHECK(f.shown() == QStringList{"a", "b", "c", "d", "e"});
}

TEST_CASE("規格 §1 情況 1：同一首出現兩次兩列都顯示，各自移除與排序") {
    Fixture f({QStringLiteral("a"), QStringLiteral("x"), QStringLiteral("a")});
    REQUIRE(f.tracks->count() == 3);
    CHECK(f.role(0, PlaylistTracks::KeyRole) == f.role(2, PlaylistTracks::KeyRole));
    CHECK(f.tracks->moveRows({2}, 1) == 1);
    CHECK(f.stored() == QStringList{"a", "a", "x"});
    f.tracks->select(0);
    REQUIRE(f.tracks->removeSelected());
    CHECK(f.stored() == QStringList{"a", "x"});
}

TEST_CASE("S-08：播放從那一首開始，佇列是整個播放清單") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    REQUIRE(f.tracks->playFrom(1));
    CHECK(f.playback.calls().last() == QStringLiteral("playFrom 1"));
    CHECK(f.playback.paths().size() == 3);
    CHECK_FALSE(f.tracks->playFrom(3));
    // 側欄項目右鍵「播放」＝從第一首播整個清單
    REQUIRE(f.controller->play(f.id));
    CHECK(f.playback.calls().last() == QStringLiteral("playFrom 0"));
}

TEST_CASE("AC-11-1：拖到清單中間＝插在那個位置，拖到空白處＝加在最後") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b")});
    TrackDragPayload p;
    p.tracks = {ref(QStringLiteral("n1")), ref(QStringLiteral("n2"))};
    REQUIRE(f.tracks->insertTracks(QString::fromUtf8(p.toJson()), 1));
    CHECK(f.stored() == QStringList{"a", "n1", "n2", "b"});
    QElapsedTimer timer;
    timer.start();
    while (f.tracks->count() != 4 && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    CHECK(f.shown() == QStringList{"a", "n1", "n2", "b"});
    p.tracks = {ref(QStringLiteral("z"))};
    REQUIRE(f.tracks->insertTracks(QString::fromUtf8(p.toJson()), -1));
    CHECK(f.stored().last() == QStringLiteral("z"));
}

TEST_CASE("S-08：右鍵選單與拖放的內容是選取的曲目，照清單順序") {
    Fixture f({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    f.tracks->select(2);
    f.tracks->toggle(0);
    const auto payload = TrackDragPayload::fromJson(f.tracks->payloadJson().toUtf8());
    REQUIRE(payload);
    REQUIRE(payload->tracks.size() == 2);
    CHECK(payload->tracks.at(0).path == ref(QStringLiteral("a")).path);
    CHECK(payload->tracks.at(1).path == ref(QStringLiteral("c")).path);
    CHECK(f.tracks->selectedTracks().size() == 2);
    f.tracks->selectAll();
    CHECK(f.tracks->selectedCount() == 3);
    f.tracks->ensureSelected(1);
    CHECK(f.tracks->selectedCount() == 3);
}
