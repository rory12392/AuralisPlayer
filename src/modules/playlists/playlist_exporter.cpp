#include "playlist_exporter.h"

#include "playlist_export.h"

#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/module_settings.h>
#include <auralis/notifier.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>

Q_DECLARE_LOGGING_CATEGORY(lcPlaylists)

namespace auralis::mod::pl {

namespace {
PlaylistExporter* g_instance = nullptr;

/// settings.ini 的 PL/exportPathMode：absolute（預設）或 relative。
const QString kPathModeKey = QStringLiteral("exportPathMode");
/// settings.ini 的 PL/lastExportFolder：上次匯出的資料夾（儲存對話框從這裡開始）。
const QString kLastFolderKey = QStringLiteral("lastExportFolder");

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistExport", text);
}

/// 預設檔名：播放清單名稱裡檔名不能用的字元換成底線。
QString suggestedFileName(QString name) {
    for (const QChar c : QStringLiteral("\\/:*?\"<>|")) {
        name.replace(c, QLatin1Char('_'));
    }
    return name + QStringLiteral(".m3u8");
}

/// 匯出結果（工作執行緒 → UI 執行緒）。
struct Outcome {
    bool ok = false;
    bool hasCue = false;
    QString error;
    int invalidPaths = 0;
};

Outcome runExport(const PlaylistStore& store, const LibraryQuery* library, qint64 id,
                  const QString& target, bool relative) {
    const auto items = store.items(id);
    if (!items) {
        return {false, false, items.error().message};
    }
    const ExportContent content = exportLines(items.value(), library);
    const QString dir = QFileInfo(target).absolutePath();
    const auto written = writeExportFile(target, buildM3u8(content.lines, dir, relative));
    if (!written) {
        return {false, false, written.error().message};
    }
    return {true, content.hasCue, {}, content.invalidPaths};
}
} // namespace

PlaylistExporter::PlaylistExporter(PlaylistStore& store, Dialogs& dialogs, Notifier& notifier,
                                   ModuleSettings* settings, const LibraryQuery* library,
                                   JobRunner* jobs, QObject* parent)
    : QObject(parent), m_store(store), m_dialogs(dialogs), m_notifier(notifier),
      m_settings(settings), m_library(library), m_jobs(jobs) {}

void PlaylistExporter::open(qint64 id) {
    const auto info = m_store.playlist(id);
    if (!info || !info.value()) {
        qCWarning(lcPlaylists) << "要匯出的播放清單不存在" << id;
        return;
    }
    m_id = id;
    m_name = info.value()->name;
    m_relative = m_settings != nullptr &&
                 m_settings->value(kPathModeKey).toString() == QStringLiteral("relative");
    emit openedChanged();
    emit opened();
}

void PlaylistExporter::chooseAndExport(bool relative) {
    if (m_id <= 0) {
        return;
    }
    m_relative = relative;
    emit openedChanged();
    if (m_settings != nullptr) {
        m_settings->setValue(kPathModeKey,
                             relative ? QStringLiteral("relative") : QStringLiteral("absolute"));
    }
    // 對話框從上次匯出的資料夾開始，沒有時用「文件」：不落在系統記得的位置（可能是音樂資料夾）
    QString folder =
        m_settings != nullptr ? m_settings->value(kLastFolderKey).toString() : QString();
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString suggested =
        QDir::toNativeSeparators(QDir(folder).filePath(suggestedFileName(m_name)));
    QPointer<PlaylistExporter> self(this);
    m_dialogs.chooseSaveFile(tr("匯出播放清單"), suggested, {tr("M3U8 播放清單 (*.m3u8)")},
                             [self](const QStringList& paths) {
                                 if (self && !paths.isEmpty()) {
                                     self->confirmAndStart(paths.first());
                                 }
                             });
}

void PlaylistExporter::confirmAndStart(const QString& chosen) {
    QString target = QDir::toNativeSeparators(chosen);
    if (QFileInfo(target).suffix().isEmpty()) {
        target += QStringLiteral(".m3u8");
    }
    if (m_settings != nullptr) {
        m_settings->setValue(kLastFolderKey,
                             QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
    }
    // 只是看一下使用者選的那個檔在不在（不讀內容），在 UI 執行緒做
    if (!QFileInfo::exists(target)) {
        start(target);
        return;
    }
    ConfirmRequest request{tr("覆蓋 %1").arg(QFileInfo(target).fileName()),
                           tr("這個檔案已經存在，覆蓋後無法復原。"), tr("覆蓋"), true};
    QPointer<PlaylistExporter> self(this);
    m_dialogs.confirm(request, [self, target](bool ok) {
        if (self && ok) {
            self->start(target);
        }
    });
}

void PlaylistExporter::start(const QString& target) {
    const qint64 id = m_id;
    const bool relative = m_relative;
    setBusy(true);
    QPointer<PlaylistExporter> self(this);
    auto done = [self, target](const Outcome& outcome, bool cancelled = false) {
        if (!self) {
            return;
        }
        self->setBusy(false);
        if (cancelled) {
            emit self->finished(target, false);
            return;
        }
        if (!outcome.ok) {
            qCWarning(lcPlaylists) << "匯出播放清單失敗" << target << outcome.error;
            self->m_notifier.notify(tr("無法匯出到 %1").arg(target), Notifier::Kind::Error);
        } else {
            QString text = tr("已匯出到 %1").arg(target);
            if (outcome.hasCue) {
                text += tr("。CUE 曲目以整軌檔的路徑匯出");
            }
            if (outcome.invalidPaths > 0) {
                text += tr("。%1 首的路徑含無效字元，沒有匯出").arg(outcome.invalidPaths);
            }
            self->m_notifier.notify(text);
        }
        emit self->finished(target, outcome.ok);
    };
    if (m_jobs == nullptr) {
        done(runExport(m_store, m_library, id, target, relative));
        return;
    }
    m_jobs->submit(tr("匯出播放清單"), [store = &m_store, library = m_library, id, target, relative,
                                        done, gate = m_gate](JobContext& context) {
        // 模組停止後才開始的工作不碰播放清單儲存與音樂庫（它們可能已經解構，P-PL-24）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        Outcome outcome = runExport(*store, library, id, target, relative);
        // 取消時不通知結果，但仍回 UI 執行緒清掉忙碌狀態（M-04 審查）
        const bool cancelled = context.isCancelled();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [done, outcome, cancelled] { done(outcome, cancelled); },
            Qt::QueuedConnection);
    });
}

void PlaylistExporter::shutdown(int timeoutMs) noexcept {
    // 匯出中途不檢查取消，所以不取消，只關閘門：還沒開始的直接結束，已經開始的等它寫完
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcPlaylists) << "停止時匯出播放清單的工作沒有在" << timeoutMs << "ms 內結束";
    }
}

void PlaylistExporter::setBusy(bool busy) {
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void PlaylistExporter::setInstance(PlaylistExporter* instance) {
    g_instance = instance;
}

PlaylistExporter* PlaylistExporter::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PlaylistExporter", "模組要在載入側欄前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::pl
