#include "gui.h"

#include "covers/cover_image_provider.h"
#include "library/open_paths.h"
#include "modules/core_services.h"
#include "modules/module_manager.h"
#include "play_entry.h"
#include "self_check.h"
#include "settings/output_settings.h"
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

#ifdef AURALIS_DEV_UI
#include "audio/capture_backend.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcGui, "auralis.app")

namespace auralis::app {

namespace {

/// 截圖前等這麼多個畫面，讓 Loader 載入的頁面與字型都畫出來。
constexpr int kFramesBeforeScreenshot = 3;
constexpr int kScreenshotSettleMs = 300;

void printLine(FILE* stream, const QString& text) {
    const QByteArray bytes = text.toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/// 畫面穩定後截圖存檔並結束事件迴圈（結束碼 0 成功、1 失敗）。
void scheduleScreenshot(QQuickWindow* window, const QString& outPath) {
    auto frames = std::make_shared<int>(0);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [=] {
        if (++*frames < kFramesBeforeScreenshot) {
            window->update();
            return;
        }
        QObject::disconnect(*connection);
        QTimer::singleShot(kScreenshotSettleMs, window, [window, outPath] {
            const QImage image = window->grabWindow();
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            const bool saved = !image.isNull() && image.save(outPath);
            printLine(saved ? stdout : stderr, saved ? QStringLiteral("截圖：%1").arg(outPath)
                                                     : QStringLiteral("截圖失敗：%1").arg(outPath));
            QCoreApplication::exit(saved ? 0 : 1);
        });
    });
    window->update();
}

/// 佇列面板一列的曲目資訊從音樂庫查（工作執行緒）；查不到時 CUE 曲目讀 .cue，其他的畫面用檔名。
std::optional<ui::QueueTrackMeta> lookupTrack(const core::LibraryQueryImpl& library,
                                              const TrackRef& track) {
    const auto found = library.findByIdentity(track.path, track.subsong);
    if (!found.hasValue() || !found.value()) {
        return cueTrackMeta(track);
    }
    const TrackInfo& info = *found.value();
    return ui::QueueTrackMeta{info.title, info.artist, info.durationMs, info.coverKey};
}

/// 正在播放面板（S-11）的目前曲目資訊（工作執行緒）：專輯的曲目數再查一次音樂庫，
/// 位元率用音樂檔大小（只讀目錄資訊，不開檔）；CUE 曲目只是整軌檔的一段，位元率不算。
std::optional<ui::NowPlayingInfo> lookupNowPlaying(const core::LibraryQueryImpl& library,
                                                   const TrackRef& track) {
    const auto found = library.findByIdentity(track.path, track.subsong);
    if (!found.hasValue() || !found.value()) {
        return std::nullopt;
    }
    const TrackInfo& info = *found.value();
    int trackTotal = 0;
    if (info.albumId > 0) {
        TrackFilter filter;
        filter.albumId = info.albumId;
        const auto albumTracks = library.tracks(filter);
        trackTotal = albumTracks.hasValue() ? static_cast<int>(albumTracks.value().size()) : 0;
    }
    const qint64 fileBytes = info.startCdFrame >= 0 ? -1 : QFileInfo(track.path).size();
    return ui::nowPlayingFrom(info, trackTotal, fileBytes);
}

/// 輸出面板（S-14）接核心：裝置清單來自 OutputRegistry（工作執行緒，只列舉不開裝置），
/// 切換與目前的選擇經 PlaybackService；選了的輸出記進 settings.ini，重新啟動後沿用。
ui::OutputHooks outputHooks(core::CoreServices& services, const QString& settingsPath) {
    ui::OutputHooks hooks;
    hooks.list = [&outputs = services.outputs] {
        QList<ui::OutputOption> options;
        for (const auto& factory : outputs.factories()) {
            const QString note = factory->note();
            for (const OutputDevice& device : factory->devices()) {
                options.append({factory->name(), device.id, device.name, note});
            }
        }
        return options;
    };
    hooks.select = [&playback = services.playback, settingsPath](const ui::OutputOption& option) {
        return core::selectOutput(playback, settingsPath,
                                  {option.backend, option.deviceId, option.deviceName});
    };
    hooks.current = [&playback = services.playback] {
        const auto choice = playback.outputChoice();
        return ui::OutputOption{choice.backend, choice.deviceId, choice.deviceName, {}};
    };
    return hooks;
}

#ifdef AURALIS_DEV_UI
/// 開發建置的截圖用：--screenshot "shell?queue=<資料夾>[&state=paused][&output=open]"
/// （另有 &panel=closed，在 runGui 處理）把資料夾加進音樂庫、
/// 掃描完把裡面的音樂檔放進佇列從第一首播，讓播放列與佇列面板有內容可截。輸出用 Capture 後端，
/// 不開實體音訊裝置、不發聲。準備好（再等曲目資訊查好、進度走一點）時呼叫 ready。
void startDemoQueue(core::CoreServices& services, const QVariantMap& params,
                    std::function<void()> ready) {
    const QString folder = params.value(QStringLiteral("queue")).toString();
    const bool paused = params.value(QStringLiteral("state")).toString() == QLatin1String("paused");
    services.playback.setOutputBackend(std::make_unique<core::CaptureBackend>());
    auto enqueue = [&services, folder, paused, ready] {
        QList<TrackRef> tracks;
        const QFileInfoList files = QDir(folder).entryInfoList(
            {QStringLiteral("*.flac"), QStringLiteral("*.mp3"), QStringLiteral("*.wav")},
            QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            tracks.append({QDir::toNativeSeparators(file.absoluteFilePath()), 0});
        }
        services.playback.playFrom(tracks, 0);
        if (paused) {
            services.playback.pause();
        }
        QTimer::singleShot(1500, &services.playback, ready);
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(&services.library, &LibraryQuery::scanFinished,
                                   &services.playback, [connection, enqueue] {
                                       QObject::disconnect(*connection);
                                       enqueue();
                                   });
    if (services.libraryManager.addFolder(folder) != core::LibraryManager::AddResult::Added) {
        QObject::disconnect(*connection);
        enqueue();
    }
}

/// 開發建置的截圖用：--screenshot "welcome?library=<資料夾>" 或
/// "settings?library=<資料夾>[&report=open]"：和按「加入音樂資料夾」一樣經 Library 加入，
/// 掃描完（每個資料夾的曲目數也查好）再呼叫 ready。
void startDemoLibrary(core::CoreServices& services, ui::LibraryController& library,
                      const QString& folder, std::function<void()> ready) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(&services.library, &LibraryQuery::scanFinished, &library,
                                   [connection, ready] {
                                       QObject::disconnect(*connection);
                                       QTimer::singleShot(500, ready);
                                   });
    if (!library.addFolder(folder)) {
        QObject::disconnect(*connection);
        ready();
    }
}
#endif

} // namespace

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
    modules.startAll();
    // 套用記住的輸出（模組的輸出路徑在 startAll 時登記，所以在它之後）；不開裝置。
    // 記住的裝置不在時用預設裝置，並在狀態列說明
    output.setNotice(core::restoreOutput(services.playback, settingsPath));
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
