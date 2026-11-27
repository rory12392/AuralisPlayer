#include "gui_screenshot.h"

#include "modules/core_services.h"
#include "shell/library_controller.h"

#ifdef AURALIS_DEV_UI
#include "audio/capture_backend.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQuickWindow>
#include <QTimer>

#include <memory>

namespace auralis::app {

namespace {

/// 截圖前等這麼多個畫面，讓 Loader 載入的頁面與字型都畫出來。
constexpr int kFramesBeforeScreenshot = 3;
constexpr int kScreenshotSettleMs = 300;
/// 這麼久沒有畫出新的一格就放棄：windows 平台的視窗畫不出來時（沒顯示、螢幕關閉或鎖定、從背景
/// 行程開而被別的視窗整個蓋住）收不到 frameSwapped，原本會一直閒置不結束（P-41）。
constexpr int kNoFrameTimeoutMs = 30000;

} // namespace

void printLine(FILE* stream, const QString& text) {
    const QByteArray bytes = text.toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
    std::fflush(stream);
}

void scheduleScreenshot(QQuickWindow* window, const QString& outPath) {
    auto frames = std::make_shared<int>(0);
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto* noFrame = new QTimer(window);
    noFrame->setSingleShot(true);
    noFrame->setInterval(kNoFrameTimeoutMs);
    QObject::connect(noFrame, &QTimer::timeout, window, [] {
        printLine(stderr, QStringLiteral("截圖等不到畫面：%1 秒內視窗沒有畫出新的一格（視窗沒顯示、"
                                         "螢幕關閉或鎖定、或被其他視窗蓋住）；改用 "
                                         "QT_QPA_PLATFORM=offscreen 截圖")
                              .arg(kNoFrameTimeoutMs / 1000));
        QCoreApplication::exit(1);
    });
    *connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [=] {
        if (++*frames < kFramesBeforeScreenshot) {
            noFrame->start();
            window->update();
            return;
        }
        noFrame->stop();
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
    noFrame->start();
    window->update();
}

#ifdef AURALIS_DEV_UI
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

} // namespace auralis::app
