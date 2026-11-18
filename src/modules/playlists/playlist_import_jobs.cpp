// PlaylistImporter 的背景工作部分：重新檢查與重新對應共用的 runJob（取消時的清理，M-04 審查）、
// 重新檢查所有播放清單（T-52）。和 playlist_import.cpp 分開放是因為單檔行數上限
// （boundary.file_length）。
#include "playlist_import.h"

#include "playlist_remap.h"
#include "playlists_controller.h"

#include <auralis/job_runner.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(lcPlaylists)

namespace auralis::mod::pl {

namespace {
QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}
} // namespace

void PlaylistImporter::runJob(std::function<void(PlaylistImporter&)> cancelled,
                              std::function<std::function<void()>()> work) {
    if (m_jobs == nullptr) {
        work()();
        return;
    }
    QPointer<PlaylistImporter> self(this);
    m_jobs->submit(tr("重新檢查播放清單"), [self, work = std::move(work), gate = m_gate,
                                            cancelled = std::move(cancelled)](JobContext& context) {
        // 模組停止後才開始的工作不碰播放清單儲存與音樂庫（它們可能已經解構，P-PL-24）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        std::function<void()> done = work();
        if (context.isCancelled()) {
            // 不套用結果，但仍回 UI 執行緒清掉這個工作設的狀態（M-04 審查）
            done = [self, cancelled] {
                if (self) {
                    cancelled(*self);
                }
            };
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(done),
                                  Qt::QueuedConnection);
    });
}

void PlaylistImporter::shutdown(int timeoutMs) noexcept {
    // 匯入與重新檢查中途不檢查取消，所以不取消，只關閘門：還沒開始的直接結束，已經開始的等它
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcPlaylists) << "停止時匯入或重新檢查播放清單的工作沒有在" << timeoutMs
                               << "ms 內結束";
    }
}

void PlaylistImporter::recheckAll() {
    QPointer<PlaylistImporter> self(this);
    const auto undo = [](PlaylistImporter& importer) {
        emit importer.rechecked(0);
    };
    runJob(undo, [self, store = &m_store, exists = m_exists, library = libraryCheck()] {
        QElapsedTimer timer;
        timer.start();
        Result<QList<qint64>> changed = mod::pl::recheckAll(*store, exists, library);
        qCInfo(lcPlaylists) << "重新檢查所有播放清單" << timer.elapsed() << "ms";
        return std::function<void()>([self, changed = std::move(changed)] {
            if (!self) {
                return;
            }
            if (!changed) {
                qCWarning(lcPlaylists) << "重新檢查播放清單失敗" << changed.error().message;
                emit self->rechecked(0);
                return;
            }
            if (!changed.value().isEmpty()) {
                self->m_playlists.reload(); // 項目數可能變了（F-1）
            }
            for (const qint64 id : changed.value()) {
                emit self->m_playlists.itemsChanged(id);
            }
            emit self->rechecked(static_cast<int>(changed.value().size()));
        });
    });
}

} // namespace auralis::mod::pl
