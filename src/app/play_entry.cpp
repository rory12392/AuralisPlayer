#include "play_entry.h"

#include "audio/capture_backend.h"
#include "library/cue_tracks.h"
#include "modules/core_services.h"
#include "shell/open_files_controller.h"
#include "single_instance.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcEntry, "auralis.app")

namespace auralis::app {

namespace {

/// 截圖前讓播放走一點、曲目資訊查好（和開發建置的示範佇列一樣）。
constexpr int kSettleMs = 1500;

QString stateText(PlaybackService::State state) {
    switch (state) {
    case PlaybackService::State::Playing:
        return QStringLiteral("播放中");
    case PlaybackService::State::Paused:
        return QStringLiteral("暫停");
    case PlaybackService::State::Stopped:
        break;
    }
    return QStringLiteral("停止");
}

} // namespace

std::optional<ui::QueueTrackMeta> cueTrackMeta(const TrackRef& track) {
    if (track.subsong == 0 || !core::isCuePath(track.path)) {
        return std::nullopt;
    }
    const auto cue = core::loadCue(track.path);
    if (!cue) {
        return std::nullopt;
    }
    for (const core::CueTrack& entry : cue.value().sheet.tracks) {
        if (entry.number != static_cast<int>(track.subsong)) {
            continue;
        }
        ui::QueueTrackMeta meta;
        // 和音樂庫收錄 CUE 曲目的寫法相同：沒有 TITLE 時「.cue 主檔名 軌號」
        meta.title = !entry.title.isEmpty() ? entry.title
                                            : QStringLiteral("%1 %2")
                                                  .arg(QFileInfo(track.path).completeBaseName())
                                                  .arg(entry.number, 2, 10, QLatin1Char('0'));
        meta.artist = !entry.performer.isEmpty() ? entry.performer : cue.value().sheet.performer;
        // 最後一軌播到整軌檔結尾，不開整軌檔就不知道長度
        meta.durationMs =
            entry.end >= 0 ? (entry.end - entry.start) * 1000 / core::kCdFramesPerSecond : -1;
        return meta;
    }
    return std::nullopt;
}

void listenForPaths(SingleInstance& instance, ui::OpenFilesController& openFiles,
                    QQuickWindow* window) {
    instance.listen(&openFiles, [&openFiles, window](const QStringList& paths) {
        openFiles.openPaths(paths);
        // 把已開的視窗叫到前面（最小化時先還原）；新的執行個體已允許這個行程搶前景
        if (window->visibility() == QWindow::Minimized) {
            window->showNormal();
        }
        window->raise();
        window->requestActivate();
    });
}

void openCommandLinePaths(core::CoreServices& services, ui::OpenFilesController& openFiles,
                          const QStringList& paths, bool screenshot, std::function<void()> ready) {
    if (screenshot) {
        services.playback.setOutputBackend(std::make_unique<core::CaptureBackend>());
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = QObject::connect(
            &openFiles, &ui::OpenFilesController::opened, &openFiles,
            [connection, &openFiles, &playback = services.playback, ready](int added) {
                QObject::disconnect(*connection);
                const auto current = playback.currentEntry();
                const QString line = QStringLiteral("開啟：加入 %1 首，%2，目前曲目：%3")
                                         .arg(added)
                                         .arg(stateText(playback.state()),
                                              current ? current->track.path : QStringLiteral("—"));
                qCInfo(lcEntry).noquote() << line;
                const QByteArray bytes = line.toUtf8() + '\n';
                std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
                std::fflush(stdout);
                QTimer::singleShot(kSettleMs, &openFiles, ready);
            });
    }
    openFiles.openPaths(paths);
}

std::optional<QString> screenshotPanelId(const QVariantList& panels, const QString& screenshotPage,
                                         const QVariantMap& params) {
    const QString panelArg = params.value(QStringLiteral("panel")).toString();
    if (screenshotPage.isEmpty() || panelArg.isEmpty() || panelArg == QLatin1String("closed")) {
        return QString();
    }
    const QString panelId = QStringLiteral("panel/") + panelArg;
    QStringList ids;
    for (const QVariant& entry : panels) {
        ids.append(entry.toMap().value(QStringLiteral("id")).toString());
    }
    if (ids.contains(panelId)) {
        return panelId;
    }
    const QByteArray bytes = QStringLiteral("沒有這個面板：%1（已登記的面板：%2）\n")
                                 .arg(panelId, ids.join(QStringLiteral("、")))
                                 .toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fflush(stderr);
    return std::nullopt;
}

} // namespace auralis::app
