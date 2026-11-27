#include "gui.h"
#include "library/migrator.h"
#include "library/open_paths.h"
#include "logging/logger.h"
#include "modules/core_services.h"
#include "modules/module_manager.h"
#include "plugins/plugin_manager.h"
#include "self_check.h"
#include "settings/data_dir.h"
#include "settings/ui_scale.h"
#include "shell/plugins_controller.h"
#include "single_instance.h"
#include "soak.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QtPlugin>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <memory>
#include <optional>

Q_LOGGING_CATEGORY(lcApp, "auralis.app")

// 主視窗的 QML 模組是靜態函式庫
Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)

namespace {

void printTo(FILE* stream, const QByteArray& text) {
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stream);
    std::fflush(stream);
}

/// --size 1600x900 → QSize；格式不對時回傳無效的 QSize。
QSize parseSize(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const auto match = pattern.match(text);
    return match.hasMatch() ? QSize(match.captured(1).toInt(), match.captured(2).toInt()) : QSize();
}

} // namespace

int main(int argc, char* argv[]) {
    QElapsedTimer sinceStart;
    sinceStart.start();
    // 介面縮放（AC-19-4）要在建立 QGuiApplication 之前決定。資料夾照同樣的規則先決定
    // （開發版沒指定資料夾時這裡不讀設定，下面照舊拒絕啟動）
    {
        const auto earlyDataDir =
            auralis::core::resolveDataDir(auralis::core::dataDirInputsBeforeApp());
        auralis::core::applyUiScaleBeforeApp(
            earlyDataDir ? earlyDataDir.value() + QStringLiteral("/settings.ini") : QString());
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("AuralisPlayer"));
    QGuiApplication::setApplicationVersion(QStringLiteral(AURALIS_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AuralisPlayer 音樂播放器"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dataDirOption(QStringLiteral("data-dir"),
                                           QStringLiteral("設定、資料庫與日誌的資料夾"),
                                           QStringLiteral("資料夾"));
    const QCommandLineOption selfCheckOption(
        QStringLiteral("self-check"),
        QStringLiteral("自我檢查：啟動核心與模組後輸出 JSON 並結束（必須搭配 --data-dir）"));
    const QCommandLineOption probeOutputOption(
        QStringLiteral("probe-output"),
        QStringLiteral("搭配 --self-check：列出輸出裝置與獨占支援的格式（唯讀查詢，不開啟裝置）"));
    const QCommandLineOption soakOption(
        QStringLiteral("soak"),
        QStringLiteral("搭配 --self-check：用 Capture 後端連續播放測試訊號這麼多秒，回報欠載次數"),
        QStringLiteral("秒"));
    const QCommandLineOption soakDsdOption(
        QStringLiteral("soak-dsd"),
        QStringLiteral("搭配 --soak：測試訊號加入 DSD64 曲目和 PCM 交替，經進階輸出模組註冊的 DSD "
                       "包裝器播放（沒有註冊時略過，寫在結果的 dsd）"));
    const QCommandLineOption soakFullscreenOption(
        QStringLiteral("fullscreen"),
        QStringLiteral(
            "搭配 --soak：soak 期間開著主視窗的全螢幕頁（AC-17-5，沒有全螢幕頁時列進 errors）"));
    const QCommandLineOption benchAlbumWallOption(
        QStringLiteral("bench-albumwall"),
        QStringLiteral(
            "搭配 --self-check（AURALIS_DEV_UI=ON 的建置）：把資料夾加進音樂庫，開 library/albums "
            "封面牆捲到底再回頂，量平均與最低 fps（門檻 50）"),
        QStringLiteral("資料夾"));
    const QCommandLineOption pageOption(QStringLiteral("page"),
                                        QStringLiteral("啟動後開啟的頁面（頁面 id）"),
                                        QStringLiteral("頁面"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"), QStringLiteral("截這個頁面後結束（shell 是主視窗本身）"),
        QStringLiteral("頁面"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("截圖的視窗大小，例：1600x900"),
                                        QStringLiteral("寬x高"), QStringLiteral("1600x900"));
    const QCommandLineOption themeOption(
        QStringLiteral("theme"), QStringLiteral("這次執行的主題：void 或 paper（不存設定）"),
        QStringLiteral("主題"));
    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("截圖的輸出檔"),
                                       QStringLiteral("檔案"));
    parser.addOptions({dataDirOption, selfCheckOption, probeOutputOption, soakOption, soakDsdOption,
                       soakFullscreenOption, benchAlbumWallOption, pageOption, screenshotOption,
                       sizeOption, themeOption, outOption});
    parser.addPositionalArgument(
        QStringLiteral("路徑"),
        QStringLiteral("加到佇列並開始播放的音樂檔或資料夾（資料夾含子資料夾，.cue 展開成各軌）"),
        QStringLiteral("[路徑…]"));
    parser.process(app);

    const bool selfCheck = parser.isSet(selfCheckOption);
    if (selfCheck && !parser.isSet(dataDirOption)) {
        printTo(stderr, QByteArrayLiteral("--self-check 必須明確指定 --data-dir <資料夾>\n"));
        return 2;
    }
    int soakSeconds = 0;
    if (parser.isSet(soakOption)) {
        bool valid = false;
        soakSeconds = parser.value(soakOption).toInt(&valid);
        if (!selfCheck || !valid || soakSeconds < auralis::app::kMinSoakSeconds) {
            printTo(stderr, QStringLiteral("--soak 要搭配 --self-check，秒數至少 %1\n")
                                .arg(auralis::app::kMinSoakSeconds)
                                .toUtf8());
            return 2;
        }
    }
    if (parser.isSet(soakDsdOption) && soakSeconds == 0) {
        printTo(stderr, QByteArrayLiteral("--soak-dsd 要搭配 --soak <秒>\n"));
        return 2;
    }
    const bool soakFullscreen = parser.isSet(soakFullscreenOption);
    if (soakFullscreen && (soakSeconds == 0 || parser.isSet(benchAlbumWallOption))) {
        printTo(stderr, QByteArrayLiteral(
                            "--fullscreen 要搭配 --soak <秒>，不能和 --bench-albumwall 同時用\n"));
        return 2;
    }
    auralis::app::GuiOptions gui;
    gui.page = parser.value(pageOption);
    QJsonObject albumWall;
    if (parser.isSet(benchAlbumWallOption)) {
#ifdef AURALIS_DEV_UI
        if (!selfCheck) {
            printTo(stderr, QByteArrayLiteral("--bench-albumwall 要搭配 --self-check\n"));
            return 2;
        }
        gui.benchAlbumWall =
            QDir::toNativeSeparators(QDir(parser.value(benchAlbumWallOption)).absolutePath());
        gui.benchResult = &albumWall;
        gui.page = QStringLiteral("library/albums");
        gui.size = parseSize(parser.value(sizeOption));
#else
        printTo(stderr,
                QByteArrayLiteral(
                    "--bench-albumwall 只有 AURALIS_DEV_UI=ON 的建置有（windows-debug）\n"));
        return 2;
#endif
    }
    if (parser.isSet(screenshotOption)) {
        gui.screenshotPage = parser.value(screenshotOption);
        gui.size = parseSize(parser.value(sizeOption));
        gui.outPath = parser.value(outOption);
        if (!gui.size.isValid() || gui.outPath.isEmpty()) {
            printTo(stderr,
                    QByteArrayLiteral("--screenshot 需要 --out <檔案>，--size 要像 1600x900\n"));
            return 2;
        }
    }
    if (parser.isSet(themeOption)) {
        gui.theme = parser.value(themeOption);
        if (gui.theme != QLatin1String("void") && gui.theme != QLatin1String("paper")) {
            printTo(stderr, QByteArrayLiteral("--theme 只能是 void 或 paper\n"));
            return 2;
        }
    }
    // 命令列的檔案與資料夾（AC-11-5）：選項與選項的值已由 parser 拿掉，剩下的才是路徑。
    // 相對路徑以這裡的目前資料夾為準（交給另一個執行個體時，它的目前資料夾不同）
    const QString workingDir = QDir::currentPath();
    for (const QString& argument : parser.positionalArguments()) {
        gui.paths.append(auralis::core::absoluteUserPath(argument, workingDir));
    }

    const auto inputs = auralis::core::currentDataDirInputs(parser.value(dataDirOption));
    const auto dataDir = auralis::core::resolveDataDir(inputs);
    if (!dataDir) {
        printTo(stderr, dataDir.error().message.toUtf8() + '\n');
        return 2;
    }
    if (!QDir().mkpath(dataDir.value())) {
        printTo(stderr, QStringLiteral("無法建立資料夾：%1\n").arg(dataDir.value()).toUtf8());
        return 2;
    }

    // 單一執行個體（AC-11-5）：同一個資料夾已有開著的視窗時，把路徑交給它就結束，
    // 不碰設定、資料庫與日誌。自我檢查與截圖是一次性的測試模式，不參與
    std::unique_ptr<auralis::app::SingleInstance> instance;
    if (!selfCheck && gui.screenshotPage.isEmpty()) {
        instance = std::make_unique<auralis::app::SingleInstance>(dataDir.value());
        if (!instance->isPrimary()) {
            if (instance->forward(gui.paths)) {
                return 0;
            }
            printTo(stderr, QByteArrayLiteral(
                                "已有開著的 AuralisPlayer，但無法把檔案交給它，請稍後再試一次\n"));
            return 1;
        }
        gui.instance = instance.get();
    }

    const auto logPath = auralis::core::installFileLogger(dataDir.value());
    if (!logPath) {
        printTo(stderr, logPath.error().message.toUtf8() + '\n');
        return 2;
    }
    qCInfo(lcApp).noquote() << "啟動 AuralisPlayer" << AURALIS_VERSION << "資料夾："
                            << dataDir.value();

    // 升級失敗（已還原備份）時不帶著半套 schema 啟動
    const QString databasePath = dataDir.value() + QStringLiteral("/library.db");
    const auto schema = auralis::core::migrateDatabase(databasePath);
    if (!schema && !selfCheck) {
        qCCritical(lcApp).noquote() << schema.error().message;
        printTo(stderr, schema.error().message.toUtf8() + '\n');
        auralis::core::uninstallFileLogger();
        return 2;
    }

    int exitCode = 0;
    {
        const QString settingsPath = dataDir.value() + QStringLiteral("/settings.ini");
        auralis::core::CoreServices services;
        if (schema) {
            services.databasePath = databasePath;
        }
        // 外掛在模組啟動前載入（<資料夾>\plugins\；只收這一版的官方外掛，T-103）。
        // 隨主程式安裝的官方外掛在程式資料夾的 plugins\（內建，T-105）
        auralis::core::PluginManager plugins(
            dataDir.value(), settingsPath, QStringLiteral(AURALIS_VERSION),
            auralis::core::officialPlugins(),
            QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));
        plugins.loadAll(services.plugins);
        // 設定頁的「外掛」分區（QML 單例 Plugins）用同一份外掛管理（T-104）
        auralis::ui::PluginsController pluginsUi(auralis::ui::PluginsBackend::from(plugins),
                                                 &services.dialogs);
        auralis::ui::PluginsController::setInstance(&pluginsUi);
        auralis::core::ModuleManager modules(services, settingsPath);

        if (selfCheck) {
            // 封面牆量測要開主視窗：runGui 建好視窗後才啟動模組，量完結束事件迴圈
            // --soak --fullscreen 也要開主視窗：soak 在 runGui 裡的另一條執行緒跑（DSD 經進階輸出
            // 模組註冊的包裝器，模組在 runGui 裡才啟動，所以跑的時候才取）
            int benchExit = 0;
            std::optional<auralis::app::SoakResult> soak;
            QString fullscreenError;
            auralis::app::FullscreenSoakWatch fullscreenWatch;
            if (soakFullscreen) {
                gui.fullscreenSoak = [&] {
                    soak = auralis::app::runSoak(soakSeconds, parser.isSet(soakDsdOption),
                                                 services.outputs.dsdSink(), &fullscreenWatch.stop);
                };
                gui.fullscreenError = &fullscreenError;
                gui.fullscreenWatch = &fullscreenWatch;
                benchExit = auralis::app::runGui(gui, services, modules, settingsPath, sinceStart);
            } else if (gui.benchAlbumWall.isEmpty()) {
                modules.startAll();
            } else {
                benchExit = auralis::app::runGui(gui, services, modules, settingsPath, sinceStart);
            }
            auralis::app::SelfCheckReport report;
            report.version = QStringLiteral(AURALIS_VERSION);
            report.dataDir = dataDir.value();
            report.devBuild = inputs.devBuild;
            report.modules = modules.statuses();
            report.plugins = plugins.list();
            report.schemaVersion = schema ? schema.value() : 0;
            if (!schema) {
                report.errors << schema.error().message;
            }
            auralis::app::collectModuleErrors(report);
            if (parser.isSet(probeOutputOption)) {
                report.outputs = auralis::app::probeOutputs(services.outputs);
            }
            if (soakSeconds > 0 && !soakFullscreen) {
                // DSD 經進階輸出模組註冊的包裝器（主程式不 include 模組）
                soak = auralis::app::runSoak(soakSeconds, parser.isSet(soakDsdOption),
                                             services.outputs.dsdSink());
            }
            if (!fullscreenError.isEmpty()) {
                report.errors << fullscreenError;
            } else if (soak) {
                report.soak = auralis::app::soakJson(*soak);
                if (soakFullscreen) {
                    report.soak->insert(QStringLiteral("fullscreen"), true);
                    report.errors << fullscreenWatch.interruptions;
                }
                report.errors << auralis::app::soakErrors(*soak);
            } else if (soakFullscreen) {
                report.errors << QStringLiteral("--fullscreen：主視窗結束碼 %1，沒有跑完長時間播放")
                                     .arg(benchExit);
            }
            if (!gui.benchAlbumWall.isEmpty()) {
                if (benchExit != 0 && albumWall.isEmpty()) {
                    albumWall.insert(QStringLiteral("error"),
                                     QStringLiteral("主視窗結束碼 %1（沒有 library/albums 頁？）")
                                         .arg(benchExit));
                }
                report.albumWall = albumWall;
                report.errors << auralis::app::albumWallErrors(albumWall);
                printTo(stderr, auralis::app::albumWallSummary(albumWall).toUtf8() + '\n');
            }
            printTo(stdout, auralis::app::toJson(report));
            exitCode = report.errors.isEmpty() ? 0 : 1;
        } else {
            // 監看音樂資料夾，並在背景對已加入的資料夾做一次增量檢查（AC-08-2、AC-08-3）；
            // 截圖是固定畫面的測試模式，不掃描
            if (gui.screenshotPage.isEmpty()) {
                services.libraryWatcher.start();
            }
            exitCode = auralis::app::runGui(gui, services, modules, settingsPath, sinceStart);
        }
        modules.stopAll();
    }

    qCInfo(lcApp) << "結束，結束碼" << exitCode;
    auralis::core::uninstallFileLogger();
    return exitCode;
}
