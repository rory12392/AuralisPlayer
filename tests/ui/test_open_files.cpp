// 開始播放的入口（AC-11-5）的介面部分：展開的曲目加到佇列最後、從第一首播，開不了的照 AC-01-3
// 在通知列說明；檔案總管拖進視窗的檔案和視窗內拖曳的曲目（TrackDropArea，AC-11-4）互不干擾。
// 用假的 PlaybackService 與假的展開函式，不讀磁碟。
#include "fake_playback_service.h"
#include "shell/open_files_controller.h"
#include "ui/dialogs_impl.h"
#include "ui/ui_registry_impl.h"

#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QDropEvent>
#include <QMimeData>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtPlugin>
#include <QtQml/qqmlextensionplugin.h>

#include <stdexcept>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)

using auralis::TrackDragPayload;
using auralis::TrackRef;
using auralis::core::OpenPathsResult;
using auralis::test::FakePlaybackService;
using auralis::ui::OpenFilesController;

namespace {

/// 記下收到的路徑，回傳預先設好的結果。
struct FakeExpander {
    OpenPathsResult result;
    QList<QStringList> calls;
    QMutex mutex;

    auralis::ui::OpenPathsExpander function() {
        return [this](const QStringList& paths) {
            const QMutexLocker lock(&mutex);
            calls.append(paths);
            return result;
        };
    }
};

/// 假的資料夾判斷（不讀磁碟）：沒有副檔名的才算資料夾。
bool isFolderByName(const QString& path) {
    return QFileInfo(path).suffix().isEmpty();
}

QList<TrackRef> tracks(const QStringList& paths) {
    QList<TrackRef> result;
    for (const auto& path : paths) {
        result.append({path, 0});
    }
    return result;
}

} // namespace

TEST_CASE("AC-11-5：展開的曲目加到佇列最後，從加入的第一首開始播放") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    expander.result.tracks = tracks({"D:/m/1.flac", "D:/m/2.flac", "D:/m/3.flac"});
    OpenFilesController controller(service, &notifier, expander.function());
    QSignalSpy opened(&controller, &OpenFilesController::opened);

    SECTION("佇列原本有曲目：接在最後，跳到加入的第一首") {
        service.reset(2, 0);
        controller.openPaths({QStringLiteral("D:\\m")});
        controller.waitForOpens();
        CHECK(expander.calls == QList<QStringList>{{QStringLiteral("D:\\m")}});
        CHECK(service.calls() == QStringList{"enqueueLast 3", "playAt 2"});
        CHECK(service.paths().mid(2) == QStringList{"D:/m/1.flac", "D:/m/2.flac", "D:/m/3.flac"});
        CHECK(service.isPlaying());
    }
    SECTION("佇列是空的：從第 0 首播") {
        service.reset(0, -1);
        controller.openPaths({QStringLiteral("D:\\m")});
        controller.waitForOpens();
        CHECK(service.calls() == QStringList{"enqueueLast 3", "playAt 0"});
    }
    REQUIRE(opened.size() == 1);
    CHECK(opened.first().first().toInt() == 3);
    // 全部都開得了：不另外通知（佇列與播放列看得到結果）
    CHECK(notifier.current().isEmpty());
}

TEST_CASE("AC-11-5：開不了的項目照 AC-01-3 的句型說明，其他照常播放") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    OpenFilesController controller(service, &notifier, expander.function());
    service.reset(0, -1);

    SECTION("一個") {
        expander.result.tracks = tracks({"D:/m/1.flac"});
        expander.result.skipped = {
            {QStringLiteral("D:\\m\\notes.txt"), QStringLiteral("不支援的格式")}};
        controller.openPaths({QStringLiteral("D:\\m")});
        controller.waitForOpens();
        CHECK(service.calls() == QStringList{"enqueueLast 1", "playAt 0"});
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("無法播放：notes.txt（不支援的格式）"));
        CHECK(notifier.current().value("kind").toString() == QStringLiteral("error"));
    }
    SECTION("好幾個：寫第一個與總數") {
        expander.result.skipped = {
            {QStringLiteral("http://auralis-test.invalid/a.flac"),
             QStringLiteral("只開啟本機檔案，不開啟網址或協定路徑")},
            {QStringLiteral("D:\\m\\b.txt"), QStringLiteral("不支援的格式")}};
        controller.openPaths({QStringLiteral("D:\\m")});
        controller.waitForOpens();
        CHECK(service.calls().isEmpty());
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("無法播放：http://auralis-test.invalid/a.flac"
                             "（只開啟本機檔案，不開啟網址或協定路徑）等 2 個項目"));
    }
    SECTION("沒有可播放的音樂檔") {
        controller.openPaths({QStringLiteral("D:\\空的")});
        controller.waitForOpens();
        CHECK(service.calls().isEmpty());
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("沒有可播放的音樂檔：空的"));
    }
}

namespace {

/// 假模組：照 T-61 定稿用 UiRegistry 登記播放清單副檔名，記下收到的路徑。
struct FakePlaylistModule {
    QList<QStringList> calls;

    void start(auralis::UiRegistry& ui) {
        ui.addFileOpenHandler({QStringLiteral("m3u"), QStringLiteral("m3u8"), QStringLiteral("pls"),
                               QStringLiteral("fpl")},
                              [this](const QStringList& paths) { calls.append(paths); });
    }
};

} // namespace

TEST_CASE("T-61 AC-2：有登記的副檔名在展開前交給模組，其餘照舊加入佇列並播放") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    auralis::core::UiRegistryImpl registry;
    FakePlaylistModule module;
    module.start(registry);
    OpenFilesController controller(service, &notifier, expander.function(), &registry);
    controller.setFolderCheck(isFolderByName);
    QSignalSpy opened(&controller, &OpenFilesController::opened);
    service.reset(0, -1);

    SECTION("只有播放清單：交給模組，不展開、不動佇列、不說「沒有可播放」") {
        controller.openPaths({QStringLiteral("D:\\m\\a.m3u"), QStringLiteral("D:\\m\\b.fpl")});
        controller.waitForOpens();
        CHECK(module.calls ==
              QList<QStringList>{{QStringLiteral("D:\\m\\a.m3u"), QStringLiteral("D:\\m\\b.fpl")}});
        CHECK(expander.calls.isEmpty());
        CHECK(service.calls().isEmpty());
        CHECK(notifier.current().isEmpty());
    }
    SECTION("混合批次：兩邊都做；資料夾不分流") {
        expander.result.tracks = tracks({"D:/m/1.flac", "D:/m/x/2.flac"});
        controller.openPaths({QStringLiteral("D:\\m\\1.flac"), QStringLiteral("D:\\m\\c.pls"),
                              QStringLiteral("D:\\m\\x")});
        controller.waitForOpens();
        CHECK(module.calls == QList<QStringList>{{QStringLiteral("D:\\m\\c.pls")}});
        CHECK(expander.calls ==
              QList<QStringList>{{QStringLiteral("D:\\m\\1.flac"), QStringLiteral("D:\\m\\x")}});
        CHECK(service.calls() == QStringList{"enqueueLast 2", "playAt 0"});
        REQUIRE(opened.size() == 1);
        CHECK(opened.first().first().toInt() == 2);
    }
    SECTION("混合批次但音樂那邊什麼都沒有：「沒有可播放」寫音樂那邊的名稱") {
        controller.openPaths({QStringLiteral("D:\\m\\a.m3u"), QStringLiteral("D:\\空的")});
        controller.waitForOpens();
        CHECK(module.calls.size() == 1);
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("沒有可播放的音樂檔：空的"));
    }
    SECTION("大小寫副檔名（.M3U8）也分流") {
        controller.openPaths({QStringLiteral("D:\\m\\LIST.M3U8")});
        controller.waitForOpens();
        CHECK(module.calls == QList<QStringList>{{QStringLiteral("D:\\m\\LIST.M3U8")}});
        CHECK(expander.calls.isEmpty());
    }
    SECTION("拖進來的檔案網址也分流") {
        controller.openUrls({QUrl::fromLocalFile(QStringLiteral("C:/清單/a.m3u8"))});
        controller.waitForOpens();
        CHECK(module.calls == QList<QStringList>{{QStringLiteral("C:\\清單\\a.m3u8")}});
    }
    SECTION("AC-4：使用者給的 UNC 路徑照原規則可以開，協定與裝置路徑不分流（由展開拒絕）") {
        controller.openPaths({QStringLiteral("\\\\nas\\share\\a.m3u"),
                              QStringLiteral("http://auralis-test.invalid/b.m3u"),
                              QStringLiteral("\\\\?\\C:\\c.m3u")});
        controller.waitForOpens();
        CHECK(module.calls == QList<QStringList>{{QStringLiteral("\\\\nas\\share\\a.m3u")}});
        CHECK(expander.calls ==
              QList<QStringList>{{QStringLiteral("http://auralis-test.invalid/b.m3u"),
                                  QStringLiteral("\\\\?\\C:\\c.m3u")}});
    }
}

TEST_CASE("T-61 AC-3：沒有模組登記時播放清單不加進佇列，提示一句") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    auralis::core::UiRegistryImpl registry; // PL 停用：沒有人登記
    OpenFilesController controller(service, &notifier, expander.function(), &registry);
    controller.setFolderCheck(isFolderByName);
    service.reset(0, -1);

    SECTION("一個") {
        controller.openPaths({QStringLiteral("D:\\m\\Mix.M3U")});
        controller.waitForOpens();
        CHECK(expander.calls.isEmpty());
        CHECK(service.calls().isEmpty());
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("播放清單模組已停用，無法匯入：Mix.M3U"));
    }
    SECTION("好幾個，混著音樂檔：音樂檔照常播放") {
        expander.result.tracks = tracks({"D:/m/1.flac"});
        controller.openPaths({QStringLiteral("D:\\m\\a.pls"), QStringLiteral("D:\\m\\1.flac"),
                              QStringLiteral("D:\\m\\b.fpl")});
        controller.waitForOpens();
        CHECK(expander.calls == QList<QStringList>{{QStringLiteral("D:\\m\\1.flac")}});
        CHECK(service.calls() == QStringList{"enqueueLast 1", "playAt 0"});
        CHECK(notifier.current().value("text").toString() ==
              QStringLiteral("播放清單模組已停用，無法匯入：a.pls 等 2 個檔案"));
    }
}

TEST_CASE("T-61 AC-2（M-04 審查）：名稱像播放清單的資料夾照資料夾展開，PL 啟用與停用都一樣") {
    // 這一項用真的資料夾（測試暫存），確認判斷檔案或資料夾的是檔案系統、不是名稱
    const QString folder = QDir::toNativeSeparators(QString::fromUtf8(AURALIS_TEST_TMP) +
                                                    QStringLiteral("/m04-review/x.m3u8"));
    QDir(folder).removeRecursively();
    REQUIRE(QDir().mkpath(folder));
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    expander.result.tracks = tracks({"D:/m/x.m3u8/1.flac"});
    auralis::core::UiRegistryImpl registry;
    FakePlaylistModule module;
    SECTION("PL 啟用") {
        module.start(registry);
    }
    SECTION("PL 停用") {}
    OpenFilesController controller(service, &notifier, expander.function(), &registry);
    service.reset(0, -1);
    controller.openPaths({folder});
    controller.waitForOpens();
    CHECK(module.calls.isEmpty());
    CHECK(expander.calls == QList<QStringList>{{folder}});
    CHECK(service.calls() == QStringList{"enqueueLast 1", "playAt 0"});
    CHECK(notifier.current().isEmpty());
    QDir(folder).removeRecursively();
}

TEST_CASE("T-61（M-04 審查）：模組的開檔處理丟例外時，通知列說無法匯入哪個檔") {
    FakePlaybackService service;
    auralis::core::NotifierImpl notifier;
    FakeExpander expander;
    auralis::core::UiRegistryImpl registry;
    registry.addFileOpenHandler({QStringLiteral("m3u")},
                                [](const QStringList&) { throw std::runtime_error("壞掉"); });
    OpenFilesController controller(service, &notifier, expander.function(), &registry);
    controller.setFolderCheck(isFolderByName);
    controller.openPaths({QStringLiteral("D:\\m\\壞.m3u"), QStringLiteral("D:\\m\\另一個.m3u")});
    controller.waitForOpens();
    CHECK(notifier.current().value("text").toString() ==
          QStringLiteral("無法匯入：壞.m3u 等 2 個檔案"));
    CHECK(notifier.current().value("kind").toString() == QStringLiteral("error"));
    controller.openPaths({QStringLiteral("D:\\m\\壞.m3u")});
    controller.waitForOpens();
    CHECK(notifier.current().value("text").toString() == QStringLiteral("無法匯入：壞.m3u"));
}

TEST_CASE("AC-11-5：拖進來的網址換成本機路徑，不是本機檔案的原樣交給展開（由它拒絕）") {
    FakePlaybackService service;
    FakeExpander expander;
    OpenFilesController controller(service, nullptr, expander.function());
    controller.openUrls({QUrl::fromLocalFile(QStringLiteral("C:/音樂/Rё∀L.wav")),
                         QUrl(QStringLiteral("http://auralis-test.invalid/a.flac"))});
    controller.waitForOpens();
    REQUIRE(expander.calls.size() == 1);
    CHECK(expander.calls.first() ==
          QStringList{QStringLiteral("C:\\音樂\\Rё∀L.wav"),
                      QStringLiteral("http://auralis-test.invalid/a.flac")});
}

TEST_CASE(
    "AC-11-5：檔案總管拖進視窗的檔案交給 OpenFiles，視窗內拖曳的曲目照舊由 TrackDropArea 接") {
    FakePlaybackService service;
    FakeExpander expander;
    OpenFilesController controller(service, nullptr, expander.function());
    OpenFilesController::setInstance(&controller);

    // 和主視窗一樣：FileDropArea 在下面蓋滿視窗，接收曲目的 TrackDropArea 在上面（上半部）
    QQmlApplicationEngine engine;
    engine.loadData(R"(
        import QtQuick
        import Auralis.Ui
        Window {
            width: 400; height: 400
            property var internalTracks: []
            // 測試不開視窗，內容區不會跟著視窗大小，所以寫明大小（主視窗用 anchors.fill）
            FileDropArea { width: 400; height: 400 }
            TrackDropArea {
                x: 0; y: 0; width: 400; height: 200
                onTracksDropped: payload => internalTracks = payload.tracks
            }
        }
    )");
    REQUIRE(engine.rootObjects().size() == 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    REQUIRE(window != nullptr);

    auto dropAt = [window](const QPoint& point, const QMimeData& mime) {
        QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        return drop.isAccepted();
    };

    QMimeData files;
    files.setUrls({QUrl::fromLocalFile(QStringLiteral("D:/m/a.flac")),
                   QUrl::fromLocalFile(QStringLiteral("D:/m/資料夾"))});
    // 播放列（下半部）與佇列面板（上半部，TrackDropArea 底下）都收得到
    for (const QPoint point : {QPoint(100, 300), QPoint(100, 100)}) {
        expander.calls.clear();
        CHECK(dropAt(point, files));
        controller.waitForOpens();
        CHECK(expander.calls == QList<QStringList>{{QStringLiteral("D:\\m\\a.flac"),
                                                    QStringLiteral("D:\\m\\資料夾")}});
    }

    TrackDragPayload payload;
    payload.tracks.append({QStringLiteral("D:/lib/x.flac"), 0});
    QMimeData internal;
    internal.setData(QString::fromLatin1(TrackDragPayload::kMimeType), payload.toJson());
    expander.calls.clear();
    CHECK(dropAt(QPoint(100, 100), internal));
    controller.waitForOpens();
    CHECK(expander.calls.isEmpty());
    CHECK(window->property("internalTracks").toList().size() == 1);

    OpenFilesController::setInstance(nullptr);
}
