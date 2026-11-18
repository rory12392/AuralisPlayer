// 核心的介面服務：UiRegistry、Navigator、Dialogs、Notifier、TrackDragPayload。
#include "ui/dialogs_impl.h"
#include "ui/ui_registry_impl.h"

#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

using auralis::ConfirmRequest;
using auralis::Notifier;
using auralis::TrackDragPayload;
using auralis::UiContribution;
using auralis::core::DialogsImpl;
using auralis::core::NavigatorImpl;
using auralis::core::NotifierImpl;
using auralis::core::UiRegistryImpl;

namespace {

UiContribution page(const char* id, int order = 0) {
    return {QString::fromLatin1(id), QString::fromLatin1(id),
            QUrl(QStringLiteral("qrc:/test/%1.qml").arg(QLatin1String(id))), order};
}

} // namespace

TEST_CASE("介面：UiRegistry 依 order 排序，同一個 id 後登記的取代先登記的") {
    UiRegistryImpl registry;
    QSignalSpy changed(&registry, &UiRegistryImpl::changed);
    registry.addSidebarPage(page("library/artists", 20));
    registry.addSidebarPage(page("library/albums", 10));
    registry.addPage(page("library/album"));
    registry.addPanel(page("panel/lyrics"));
    CHECK(changed.count() == 4);

    const auto sidebar = registry.sidebarPages();
    REQUIRE(sidebar.size() == 2);
    CHECK(sidebar[0].toMap().value("id").toString() == QStringLiteral("library/albums"));
    CHECK(sidebar[1].toMap().value("id").toString() == QStringLiteral("library/artists"));
    CHECK(registry.panels().size() == 1);
    CHECK(registry.pageIds().contains(QStringLiteral("library/album")));
    CHECK_FALSE(registry.pageIds().contains(QStringLiteral("panel/lyrics")));

    auto replacement = page("library/albums", 10);
    replacement.source = QUrl(QStringLiteral("qrc:/test/other.qml"));
    registry.addSidebarPage(replacement);
    CHECK(registry.sidebarPages().size() == 2);
    CHECK(registry.pageSource(QStringLiteral("library/albums")) == replacement.source);
}

TEST_CASE("T-28：工具列常駐項目依 order 排序，和頁面分開、同 id 後登記的取代先登記的") {
    UiRegistryImpl registry;
    QSignalSpy changed(&registry, &UiRegistryImpl::changed);
    registry.addToolbarItem(page("test/b", 20));
    registry.addToolbarItem(page("test/a", 10));
    CHECK(changed.count() == 2);

    const auto items = registry.toolbarItems();
    REQUIRE(items.size() == 2);
    CHECK(items[0].toMap().value("id").toString() == QStringLiteral("test/a"));
    CHECK(items[1].toMap().value("id").toString() == QStringLiteral("test/b"));
    // 工具列項目不是頁面，Navigator 開不了
    CHECK_FALSE(registry.pageIds().contains(QStringLiteral("test/a")));

    registry.addToolbarItem(page("test/a", 30));
    const auto replaced = registry.toolbarItems();
    REQUIRE(replaced.size() == 2);
    CHECK(replaced[1].toMap().value("id").toString() == QStringLiteral("test/a"));
}

TEST_CASE("T-61：開檔處理依副檔名查（只看字串、不分大小寫），同一個副檔名後登記的取代先登記的") {
    UiRegistryImpl registry;
    int first = 0;
    int second = 0;
    registry.addFileOpenHandler({QStringLiteral("m3u"), QStringLiteral("PLS")},
                                [&first](const QStringList&) { ++first; });

    const auto m3u = registry.fileOpenHandler(QStringLiteral("D:\\m\\a.M3U"));
    REQUIRE(m3u != nullptr);
    // 同一次登記的副檔名是同一個物件（呼叫端用它把同一批合併成一次呼叫）
    CHECK(registry.fileOpenHandler(QStringLiteral("D:/m/b.pls")) == m3u);
    (*m3u)({});
    CHECK(first == 1);
    CHECK(registry.fileOpenHandler(QStringLiteral("D:\\m\\a.flac")) == nullptr);
    CHECK(registry.fileOpenHandler(QStringLiteral("D:\\m.m3u\\沒有副檔名")) == nullptr);
    CHECK(registry.fileOpenHandler(QStringLiteral("D:\\m\\.m3u")) == nullptr);
    CHECK(registry.fileOpenHandler(QStringLiteral("D:\\m\\a.m3u8")) == nullptr);

    registry.addFileOpenHandler({QStringLiteral("m3u")},
                                [&second](const QStringList&) { ++second; });
    (*registry.fileOpenHandler(QStringLiteral("a.m3u")))({});
    CHECK(second == 1);
    // 沒被取代的副檔名照舊
    (*registry.fileOpenHandler(QStringLiteral("a.pls")))({});
    CHECK(first == 2);
}

TEST_CASE("介面：Navigator 開頁、返回，沒有登記的頁面開不了") {
    UiRegistryImpl registry;
    NavigatorImpl navigator(registry);
    registry.addSidebarPage(page("library/albums"));
    registry.addPage(page("library/album"));
    registry.addFullscreenPage(page("fullscreen"));
    QSignalSpy changed(&navigator, &NavigatorImpl::currentChanged);

    CHECK_FALSE(navigator.open(QStringLiteral("沒有這頁")));
    CHECK(navigator.currentPage().isEmpty());
    CHECK_FALSE(navigator.canGoBack());

    REQUIRE(navigator.open(QStringLiteral("library/albums")));
    REQUIRE(navigator.open(QStringLiteral("library/album"), {{QStringLiteral("albumId"), 12}}));
    CHECK(navigator.params().value(QStringLiteral("albumId")).toInt() == 12);
    CHECK(navigator.currentSource() == QUrl(QStringLiteral("qrc:/test/library/album.qml")));
    CHECK_FALSE(navigator.fullscreen());
    REQUIRE(navigator.open(QStringLiteral("fullscreen")));
    CHECK(navigator.fullscreen());

    CHECK(navigator.back());
    CHECK(navigator.currentPage() == QStringLiteral("library/album"));
    CHECK(navigator.back());
    CHECK(navigator.currentPage() == QStringLiteral("library/albums"));
    CHECK_FALSE(navigator.back());
    CHECK(changed.count() >= 5);
}

TEST_CASE("T-76：正在播放面板的區塊槽位依 order 排序，不是頁面、同 id 後登記的取代先登記的") {
    UiRegistryImpl registry;
    QSignalSpy changed(&registry, &UiRegistryImpl::changed);
    CHECK(registry.nowPlayingSections().isEmpty());
    registry.addNowPlayingSection(page("test/b", 20));
    registry.addNowPlayingSection(page("test/a", 10));
    CHECK(changed.count() == 2);

    const auto sections = registry.nowPlayingSections();
    REQUIRE(sections.size() == 2);
    CHECK(sections[0].toMap().value("id").toString() == QStringLiteral("test/a"));
    CHECK(sections[1].toMap().value("id").toString() == QStringLiteral("test/b"));
    CHECK_FALSE(registry.pageIds().contains(QStringLiteral("test/a")));

    registry.addNowPlayingSection(page("test/a", 30));
    const auto replaced = registry.nowPlayingSections();
    REQUIRE(replaced.size() == 2);
    CHECK(replaced[1].toMap().value("id").toString() == QStringLiteral("test/a"));
}

TEST_CASE("T-76：進出全螢幕開登記的全螢幕頁（order 最小的），沒有登記時不做事") {
    UiRegistryImpl registry;
    NavigatorImpl navigator(registry);
    registry.addSidebarPage(page("library/albums"));
    REQUIRE(navigator.open(QStringLiteral("library/albums")));

    // 沒有全螢幕頁（LYR 停用）
    CHECK(navigator.fullscreenPage().isEmpty());
    CHECK_FALSE(navigator.enterFullscreen());
    CHECK_FALSE(navigator.toggleFullscreen());
    CHECK_FALSE(navigator.leaveFullscreen());
    CHECK(navigator.currentPage() == QStringLiteral("library/albums"));

    QSignalSpy changed(&navigator, &NavigatorImpl::currentChanged);
    registry.addFullscreenPage(page("fullscreen", 20));
    registry.addFullscreenPage(page("test/other-fullscreen", 30));
    CHECK(changed.count() >= 1); // 晚登記時 QML 的 fullscreenPage 也要跟著更新
    CHECK(navigator.fullscreenPage() == QStringLiteral("fullscreen"));

    // 進入：開全螢幕頁；離開：回到原頁
    REQUIRE(navigator.enterFullscreen());
    CHECK(navigator.fullscreen());
    CHECK(navigator.currentPage() == QStringLiteral("fullscreen"));
    CHECK(navigator.enterFullscreen()); // 已經在全螢幕：不多記一筆歷史
    REQUIRE(navigator.leaveFullscreen());
    CHECK_FALSE(navigator.fullscreen());
    CHECK(navigator.currentPage() == QStringLiteral("library/albums"));
    CHECK_FALSE(navigator.leaveFullscreen()); // 不在全螢幕時不做事
    CHECK(navigator.currentPage() == QStringLiteral("library/albums"));

    // F11：切換
    REQUIRE(navigator.toggleFullscreen());
    CHECK(navigator.fullscreen());
    REQUIRE(navigator.toggleFullscreen());
    CHECK(navigator.currentPage() == QStringLiteral("library/albums"));

    // 一開始就在全螢幕（沒有上一頁）：離開時到第一個側欄頁
    NavigatorImpl fresh(registry);
    REQUIRE(fresh.open(QStringLiteral("fullscreen")));
    REQUIRE(fresh.leaveFullscreen());
    CHECK(fresh.currentPage() == QStringLiteral("library/albums"));
}

TEST_CASE("T-76：切換面板頁籤只認登記的面板") {
    UiRegistryImpl registry;
    NavigatorImpl navigator(registry);
    registry.addPanel(page("panel/now-playing", 10));
    QSignalSpy requested(&navigator, &NavigatorImpl::panelRequested);

    // 沒有登記歌詞面板（LYR 停用）：不做事
    CHECK_FALSE(navigator.showPanel(QStringLiteral("panel/lyrics")));
    CHECK(requested.isEmpty());

    registry.addPanel(page("panel/lyrics", 20));
    REQUIRE(navigator.showPanel(QStringLiteral("panel/lyrics")));
    REQUIRE(requested.count() == 1);
    CHECK(requested[0][0].toString() == QStringLiteral("panel/lyrics"));
    // 面板不是頁面：目前頁不變
    CHECK(navigator.currentPage().isEmpty());
}

TEST_CASE("介面：Dialogs 把請求交給 QML，回覆後呼叫模組的回呼") {
    DialogsImpl dialogs;
    QSignalSpy requested(&dialogs, &DialogsImpl::requested);

    QStringList chosen{QStringLiteral("尚未回覆")};
    dialogs.chooseFolder(QStringLiteral("加入音樂資料夾"),
                         [&](const QStringList& paths) { chosen = paths; });
    REQUIRE(requested.count() == 1);
    const int folderId = requested[0][0].toInt();
    CHECK(requested[0][1].toString() == QStringLiteral("folder"));
    CHECK(dialogs.pendingCount() == 1);
    dialogs.respond(folderId, QVariantList{QUrl::fromLocalFile(QStringLiteral("D:/音樂/ＮieR"))});
    CHECK(chosen == QStringList{QStringLiteral("D:\\音樂\\ＮieR")});
    CHECK(dialogs.pendingCount() == 0);
    dialogs.respond(folderId, QVariantList{}); // 重複回覆不做事

    int answer = -1;
    dialogs.confirm(ConfirmRequest{QStringLiteral("刪除播放清單「通勤」"),
                                   QStringLiteral("只刪除播放清單，不會刪除音樂檔。"),
                                   QStringLiteral("刪除播放清單"), true},
                    [&](bool confirmed) { answer = confirmed ? 1 : 0; });
    REQUIRE(requested.count() == 2);
    CHECK(requested[1][2].toMap().value(QStringLiteral("destructive")).toBool());
    dialogs.respond(requested[1][0].toInt(), false);
    CHECK(answer == 0);

    dialogs.chooseFiles(QStringLiteral("匯入播放清單"), {},
                        [&](const QStringList& paths) { chosen = paths; });
    dialogs.respond(requested[2][0].toInt(), QVariantList{}); // 取消
    CHECK(chosen.isEmpty());
}

TEST_CASE("介面：Notifier 新通知取代舊的，動作按鈕呼叫回呼後收起") {
    NotifierImpl notifier;
    int undone = 0;
    notifier.notify(QStringLiteral("已移除 3 首"), Notifier::Kind::Info, QStringLiteral("復原"),
                    [&] { ++undone; });
    const int first = notifier.current().value(QStringLiteral("id")).toInt();
    CHECK(notifier.current().value(QStringLiteral("actionText")).toString() ==
          QStringLiteral("復原"));

    notifier.notify(QStringLiteral("無法讀取 D:\\音樂"), Notifier::Kind::Error);
    CHECK(notifier.current().value(QStringLiteral("kind")).toString() == QStringLiteral("error"));
    notifier.triggerAction(first); // 已被取代，不做事
    CHECK(undone == 0);

    notifier.notify(QStringLiteral("已清空佇列"), Notifier::Kind::Info, QStringLiteral("復原"),
                    [&] { ++undone; });
    notifier.triggerAction(notifier.current().value(QStringLiteral("id")).toInt());
    CHECK(undone == 1);
    CHECK(notifier.current().isEmpty());
}

TEST_CASE("介面：TrackDragPayload 的 JSON 往返，不是這個格式時回傳空") {
    TrackDragPayload payload;
    payload.tracks = {{QStringLiteral("D:\\音樂\\Rё∀L.wav"), 0},
                      {QStringLiteral("D:\\音樂\\album.cue"), 3}};
    const auto back = TrackDragPayload::fromJson(payload.toJson());
    REQUIRE(back.has_value());
    REQUIRE(back->tracks.size() == 2);
    CHECK(back->tracks[0].path == payload.tracks[0].path);
    CHECK(back->tracks[1].subsong == 3u);
    CHECK(back->query.isEmpty());

    CHECK_FALSE(TrackDragPayload::fromJson("not json").has_value());
    CHECK_FALSE(TrackDragPayload::fromJson(R"({"tracks":[{"subsong":1}]})").has_value());
    CHECK(QLatin1String(TrackDragPayload::kMimeType) ==
          QLatin1String("application/x-auralis-tracks"));
}
