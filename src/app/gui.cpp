#include "gui.h"

#include "covers/cover_image_provider.h"
#include "gui_hooks.h"
#include "gui_screenshot.h"
#include "library/open_paths.h"
#include "modules/core_services.h"
#include "modules/module_manager.h"
#include "play_entry.h"
#include "self_check.h"
#include "settings/output_settings.h"
#include "settings/ui_scale.h"
#include "settings/ui_scale_controller.h"
#include "settings/volume_settings.h"
#include "shell/core_bindings.h"
#include "shell/library_controller.h"
#include "shell/open_files_controller.h"
#include "shell/output_controller.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "shell/shell_state.h"
#include "single_instance.h"
#include "soak.h"
#include "theme/theme_controller.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include <QThread>
#include <QUrlQuery>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcGui, "auralis.app")

namespace auralis::app {

int runGui(const GuiOptions& options, core::CoreServices& services, core::ModuleManager& modules,
           const QString& settingsPath, const QElapsedTimer& sinceStart) {
    // 只用自己的共用元件畫外觀；Basic 是沒有平台外觀的樣式
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qCInfo(lcGui).noquote() << "附帶字型：" << ui::loadBundledFonts().join(QStringLiteral("、"));

    // 主題在 QML 引擎建立前決定，第一個畫面就是正確的深淺色
    ui::ThemeController theme(settingsPath, QGuiApplication::styleHints());
    if (!options.theme.isEmpty()) {
        theme.overrideMode(options.theme);
    }
    QCoreApplication::instance()->installNativeEventFilter(&theme);
    // 介面縮放（AC-19-4）：比例在 QGuiApplication 建立前就決定了，這裡只給設定頁用
    const core::UiScaleStartup& scale = core::startupUiScale();
    qCInfo(lcGui).noquote() << QStringLiteral("介面縮放：設定 %1，這次 %2")
                                   .arg(scale.setting, scale.applied > 0
                                                           ? core::percentText(scale.applied)
                                                           : QStringLiteral("跟隨系統"));
    core::UiScaleController uiScale(settingsPath, scale.setting, [&services] {
        return core::monitorOfWindow(services.platformWindow.hwnd());
    });
    core::UiScaleController::setInstance(&uiScale);
    ui::ShellState shell(settingsPath);
    if (options.size.isValid()) {
        shell.useFixedSize(options.size.width(), options.size.height());
    }
    // 套用記住的音量（AC-02-7），之後改了就記下來；在畫面建立前，第一個畫面就是對的值
    const core::VolumeSettings volumeSettings(services.playback, settingsPath);
    // 播放列（S-02）與佇列面板（S-13）：PlaybackService 經 Player 給 QML
    ui::QueueModel queue(services.playback, &services.notifier,
                         [&library = services.library](const TrackRef& track) {
                             return lookupTrack(library, track);
                         });
    // 只帶查詢條件的拖放與右鍵內容（超過 5,000 首）向音樂庫取曲目（T-62）
    queue.setDragQueryResolver([&library = services.library](const QString& query) {
        return library.tracksForDragQuery(query);
    });
    ui::PlayerController player(services.playback, queue,
                                [&library = services.library](const TrackRef& track) {
                                    return lookupNowPlaying(library, track);
                                });
    // 輸出按鈕與輸出面板（S-02、S-14），播放錯誤的通知（AC-01-3、AC-02-4）
    ui::OutputController output(services.playback, &services.notifier,
                                outputHooks(services, settingsPath));
    // 歡迎畫面、設定頁的音樂資料夾與掃描報告（S-01、S-16、S-17）
    ui::LibraryController library(services.libraryManager, services.library, &services.dialogs,
                                  &services.notifier);
    // 開始播放的入口（AC-11-5）：拖進視窗、命令列、另一個執行個體轉來的檔案與資料夾；
    // 模組登記的副檔名（PL 的播放清單檔）先分流給模組（T-61）
    ui::OpenFilesController openFiles(
        services.playback, &services.notifier,
        [](const QStringList& paths) { return core::expandOpenPaths(paths); }, &services.ui);
    ui::OpenFilesController::setInstance(&openFiles);
    ui::LibraryController::setInstance(&library);
    ui::ThemeController::setInstance(&theme);
    ui::ShellState::setInstance(&shell);
    ui::PlayerController::setInstance(&player);
    ui::OutputController::setInstance(&output);
    ui::bindCoreServices(services);
    // 面板頁籤的順序：正在播放、歌詞、佇列（order 小的在前）
    services.ui.addPanel({QStringLiteral("panel/now-playing"), QObject::tr("正在播放"),
                          QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/NowPlayingPanel.qml")),
                          10});
    services.ui.addPanel({QStringLiteral("panel/queue"), QObject::tr("佇列"),
                          QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/QueuePanel.qml")), 30});
    services.ui.addPage({QStringLiteral("welcome"), QObject::tr("加入音樂資料夾"),
                         QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/WelcomePage.qml")), 0});
    // 登記後側欄底部出現「設定」，模組用 addSettingsSection 加區塊
    services.ui.addPage({QStringLiteral("settings"), QObject::tr("設定"),
                         QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/SettingsPage.qml")),
                         900});
#ifdef AURALIS_DEV_UI
    services.ui.addPage({QStringLiteral("dev/ui"), QObject::tr("元件展示"),
                         QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/dev/DevUiPage.qml")), 1000});
#endif

    QQmlApplicationEngine engine;
    // 封面：image://cover/<key>（引擎擁有提供者）
    engine.addImageProvider(QLatin1String(CoverService::kImageProviderId),
                            new core::CoverImageProvider(services.covers));
    engine.loadFromModule(QStringLiteral("Auralis.App"), QStringLiteral("Main"));
    auto* window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        qCCritical(lcGui) << "主視窗建立失敗";
        return 1;
    }
    services.platformWindow.setHwnd(static_cast<quintptr>(window->winId()));
    if (!options.size.isValid()) {
        // 關閉時記下的視窗位置是這次比例的邏輯座標：一起記下這次實際套用的比例，
        // 下次啟動用它認出螢幕（固定大小的截圖模式不記位置，也不記比例）。主視窗只在一般
        // 視窗時記位置（WindowStateKeeper.save），所以用同一個條件決定記不記比例
        QObject::connect(window, &QQuickWindow::closing, window,
                         [settingsPath, applied = scale.applied, window] {
                             core::saveAppliedUiScaleOnClose(
                                 settingsPath, applied, window->visibility() == QWindow::Windowed);
                         });
    }
    modules.startAll();
    // 套用記住的輸出（模組的輸出路徑在 startAll 時登記，所以在它之後）；不開裝置。
    // 記住的裝置不在時用預設裝置，並在狀態列說明
    // 記住的介面縮放這次放不下、改用跟隨系統時，也在狀態列的同一個位置說明
    QStringList notices{core::restoreOutput(services.playback, settingsPath), scale.notice};
    notices.removeAll(QString());
    output.setNotice(notices.join(QStringLiteral("　")));
    output.refreshDevices();

    // 頁面 id 可以帶開頁參數：dev/ui?section=lists&open=menu
    const QString pageArg =
        options.screenshotPage.isEmpty() ? options.page : options.screenshotPage;
    const QString page = pageArg.section(QLatin1Char('?'), 0, 0);
    QVariantMap params;
    const QUrlQuery query(pageArg.section(QLatin1Char('?'), 1));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        params.insert(key, value);
    }
    if (!page.isEmpty() && page != QLatin1String("shell") &&
        !services.navigator.open(page, params)) {
        printLine(stderr, QStringLiteral("沒有這個頁面：%1").arg(page));
        QCoreApplication::processEvents();
        return 2;
    }
    if (page.isEmpty() && !library.hasFolders()) {
        // 還沒有音樂資料夾：歡迎畫面取代內容區（畫面規格 S-01）
        services.navigator.open(QStringLiteral("welcome"));
    } else if (page.isEmpty() && !services.ui.sidebarPages().isEmpty()) {
        services.navigator.open(
            services.ui.sidebarPages().constFirst().toMap().value(QStringLiteral("id")).toString());
    }
    // 截圖的 &panel=：截圖前才切面板頁籤（佇列曲目數改變時頁籤會重建、回到第一個），和 Ctrl+L
    // 一樣走 Navigator.showPanel（收合時先展開）；對不到已登記的面板時失敗
    const auto panelId = screenshotPanelId(services.ui.panels(), options.screenshotPage, params);
    if (!panelId) {
        QCoreApplication::processEvents();
        return 2;
    }
    const auto screenshot = [&services, window, outPath = options.outPath, panelId = *panelId] {
        if (!panelId.isEmpty() && services.navigator.showPanel(panelId)) {
            printLine(stdout, QStringLiteral("面板：%1").arg(panelId));
        }
        scheduleScreenshot(window, outPath);
    };

    // 效能基準：程式啟動到模組都啟動後的第一個畫面（介面規範 §1，PRD 目標 ≤ 2 秒）
    auto measured = std::make_shared<QMetaObject::Connection>();
    *measured =
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [measured, sinceStart] {
            QObject::disconnect(*measured);
            qCInfo(lcGui).noquote() << "啟動到可操作：" << sinceStart.elapsed() << "ms";
        });

    bool demoQueue = false;
#ifdef AURALIS_DEV_UI
    demoQueue = page == QLatin1String("shell") && params.contains(QStringLiteral("queue"));
    if (demoQueue) {
        // &output=open：截圖前打開輸出面板（S-14）
        const bool openOutput =
            params.value(QStringLiteral("output")).toString() == QLatin1String("open");
        // &panel=closed：收合正在播放面板，播放列出現封面縮圖（S-02）
        if (params.value(QStringLiteral("panel")).toString() == QLatin1String("closed")) {
            shell.setProperty("panelOpen", false);
        }
        startDemoQueue(services, params, [options, &output, openOutput, screenshot] {
            if (openOutput) {
                output.setPanelOpen(true);
            }
            if (!options.screenshotPage.isEmpty()) {
                screenshot();
            }
        });
    } else if (!options.benchAlbumWall.isEmpty()) {
        // 封面牆捲動量測（AC-10-2）：資料夾掃描完再量，結果交給 main 印進自我檢查的 JSON
        demoQueue = true;
        startDemoLibrary(services, library, options.benchAlbumWall, [window, options] {
            startAlbumWallBench(window, [options](QJsonObject result) {
                *options.benchResult = std::move(result);
                QCoreApplication::exit(0);
            });
        });
    } else if (params.contains(QStringLiteral("library"))) {
        // 歡迎畫面與設定頁的截圖：先加入資料夾、掃描完再截
        demoQueue = true;
        startDemoLibrary(services, library, params.value(QStringLiteral("library")).toString(),
                         [options, screenshot] {
                             if (!options.screenshotPage.isEmpty()) {
                                 screenshot();
                             }
                         });
    }
#endif
    if (options.instance != nullptr) {
        listenForPaths(*options.instance, openFiles, window);
    }
    if (!options.paths.isEmpty() && !demoQueue) {
        // 截圖時開好、播一點才截（&panel=queue：右側面板切到佇列）
        demoQueue = !options.screenshotPage.isEmpty();
        openCommandLinePaths(services, openFiles, options.paths, demoQueue, [options, screenshot] {
            if (!options.screenshotPage.isEmpty()) {
                screenshot();
            }
        });
    }
    if (!options.screenshotPage.isEmpty() && !demoQueue) {
        screenshot();
    }
    // 全螢幕下的長時間播放（AC-17-5）：播放在自己的執行緒，UI 執行緒照常畫全螢幕頁
    std::unique_ptr<QThread> soakThread;
    if (options.fullscreenSoak) {
        *options.fullscreenError = openSoakFullscreen(services.navigator);
        if (!options.fullscreenError->isEmpty()) {
            return 0;
        }
        watchSoakFullscreen(services.navigator, *options.fullscreenWatch);
        soakThread.reset(QThread::create(options.fullscreenSoak));
        QObject::connect(soakThread.get(), &QThread::finished, window,
                         [] { QCoreApplication::exit(0); });
        soakThread->start();
    }
    const int exitCode = QGuiApplication::exec();
    if (soakThread) {
        finishSoakAfterUi(*soakThread, *options.fullscreenWatch);
    }
    if (options.instance != nullptr) {
        options.instance->stop();
    }
    modules.stopAll();
    return exitCode;
}

} // namespace auralis::app
