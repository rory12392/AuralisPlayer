#include "shell/open_files_controller.h"

#include "ui/ui_registry_impl.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QQmlEngine>

#include <algorithm>
#include <exception>
#include <vector>

#include <windows.h>

Q_LOGGING_CATEGORY(lcOpen, "auralis.open")

namespace auralis::ui {

namespace {

OpenFilesController* g_instance = nullptr;

/// 播放清單檔（DESIGN §7：拖進視窗＝匯入）。PL 登記它們；沒有登記時不當成音樂檔，在通知列說明。
const QStringList kPlaylistExtensions{QStringLiteral("m3u"), QStringLiteral("m3u8"),
                                      QStringLiteral("pls"), QStringLiteral("fpl")};

/// 通知列寫檔名或資料夾名；網址這類不是檔案路徑的寫整串（使用者才認得出是哪一個）。
QString displayName(const QString& path) {
    const QString name = core::checkUserPath(path) ? QFileInfo(path).fileName() : QString();
    return name.isEmpty() ? path : name;
}

/// 使用者給的路徑本身是不是資料夾（Architecture §8：使用者給的路徑可以查）。GetFileAttributesW
/// 回的是連結本身的屬性，不跟隨；查不到時當成檔案，交給模組照原本的方式回報。工作執行緒。
bool isFolderPath(const QString& path) {
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace

OpenFilesController::OpenFilesController(PlaybackService& service, Notifier* notifier,
                                         OpenPathsExpander expander,
                                         const core::UiRegistryImpl* registry, QObject* parent)
    : QObject(parent), m_service(service), m_notifier(notifier), m_expander(std::move(expander)),
      m_isFolder(isFolderPath), m_registry(registry) {
    m_pool.setMaxThreadCount(1);
}

OpenFilesController::~OpenFilesController() {
    m_pool.waitForDone();
}

void OpenFilesController::openUrls(const QList<QUrl>& urls) {
    QStringList paths;
    for (const QUrl& url : urls) {
        // 不是本機檔案的（http: 這類）原樣交給展開，由它照「只開啟本機檔案」略過並說明
        paths.append(url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile())
                                       : url.toString());
    }
    openPaths(paths);
}

bool OpenFilesController::routedByName(const QString& path) const {
    // 只看字串：協定、\\?\、裝置名稱這類照舊交給展開拒絕；使用者給的 UNC 路徑照原規則可以開
    if (!core::checkUserPath(path)) {
        return false;
    }
    return (m_registry != nullptr && m_registry->fileOpenHandler(path) != nullptr) ||
           kPlaylistExtensions.contains(QFileInfo(path).suffix().toLower());
}

QStringList OpenFilesController::routeToModules(const QStringList& files) {
    QStringList rest;
    // 同一個處理函式的路徑合併成一次呼叫，照第一次出現的順序
    std::vector<std::pair<std::shared_ptr<const FileOpenHandler>, QStringList>> routed;
    QStringList unhandled;
    for (const QString& path : files) {
        auto handler = m_registry != nullptr ? m_registry->fileOpenHandler(path) : nullptr;
        if (handler != nullptr) {
            const auto it = std::find_if(routed.begin(), routed.end(),
                                         [&](const auto& entry) { return entry.first == handler; });
            if (it == routed.end()) {
                routed.emplace_back(std::move(handler), QStringList{path});
            } else {
                it->second.append(path);
            }
        } else if (kPlaylistExtensions.contains(QFileInfo(path).suffix().toLower())) {
            unhandled.append(path);
        } else {
            rest.append(path);
        }
    }
    for (const auto& [handler, handled] : routed) {
        qCInfo(lcOpen).noquote() << "交給模組：" << handled.join(QStringLiteral("、"));
        bool failed = true;
        try {
            (*handler)(handled);
            failed = false;
        } catch (const std::exception& error) {
            qCWarning(lcOpen).noquote() << "模組處理開檔失敗：" << error.what();
        } catch (...) {
            qCWarning(lcOpen).noquote() << "模組處理開檔失敗（不明的例外）";
        }
        // 技術細節只寫日誌；使用者拖檔後不能像沒反應（M-04 審查）
        if (failed && m_notifier != nullptr) {
            const QString name = displayName(handled.constFirst());
            m_notifier->notify(handled.size() == 1
                                   ? tr("無法匯入：%1").arg(name)
                                   : tr("無法匯入：%1 等 %2 個檔案").arg(name).arg(handled.size()),
                               Notifier::Kind::Error);
        }
    }
    if (!unhandled.isEmpty()) {
        qCInfo(lcOpen).noquote() << "播放清單模組已停用，略過："
                                 << unhandled.join(QStringLiteral("、"));
        if (m_notifier != nullptr) {
            const QString name = displayName(unhandled.constFirst());
            m_notifier->notify(unhandled.size() == 1
                                   ? tr("播放清單模組已停用，無法匯入：%1").arg(name)
                                   : tr("播放清單模組已停用，無法匯入：%1 等 %2 個檔案")
                                         .arg(name)
                                         .arg(unhandled.size()),
                               Notifier::Kind::Info);
        }
    }
    return rest;
}

void OpenFilesController::openPaths(const QStringList& allPaths) {
    if (allPaths.isEmpty()) {
        return;
    }
    // 名稱在 UI 執行緒看（登記表只在 UI 執行緒用）；是檔案還是資料夾在工作執行緒查
    // （可能在網路上），只有檔案才分流，名稱像清單的資料夾照原本展開（M-04 審查）。
    // 處理函式回 UI 執行緒才呼叫
    QList<bool> byName;
    for (const QString& path : allPaths) {
        byName.append(routedByName(path));
    }
    m_pool.start([this, allPaths, byName, expander = m_expander, isFolder = m_isFolder] {
        QStringList files;
        QStringList paths;
        for (qsizetype i = 0; i < allPaths.size(); ++i) {
            (byName.at(i) && !isFolder(allPaths.at(i)) ? files : paths).append(allPaths.at(i));
        }
        const core::OpenPathsResult result =
            paths.isEmpty() ? core::OpenPathsResult() : expander(paths);
        QMetaObject::invokeMethod(
            this,
            [this, files, result, paths] {
                // 展開期間模組可能被停用：沒人處理的檔案再走一次（這次名稱不算，照常展開）
                const QStringList rest = routeToModules(files);
                if (!paths.isEmpty()) {
                    apply(result, paths);
                } else if (rest.isEmpty()) {
                    emit opened(0);
                }
                if (!rest.isEmpty()) {
                    openPaths(rest);
                }
            },
            Qt::QueuedConnection);
    });
}

void OpenFilesController::waitForOpens() {
    m_pool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

void OpenFilesController::apply(const core::OpenPathsResult& result, const QStringList& paths) {
    if (!result.tracks.isEmpty()) {
        // 加到最後再跳到加入的第一首：原本的佇列不動。
        // queue() 就是播放順序，隨機時加入的也排在最後
        const auto start = static_cast<int>(m_service.queue().size());
        m_service.enqueueLast(result.tracks);
        m_service.playAt(start);
    }
    qCInfo(lcOpen).noquote() << "開啟" << paths.size() << "個路徑：加入佇列" << result.tracks.size()
                             << "首、略過" << result.skipped.size() << "個";
    for (const auto& skipped : result.skipped) {
        qCInfo(lcOpen).noquote() << "略過：" << skipped.path << skipped.error;
    }
    if (m_notifier != nullptr) {
        if (!result.skipped.isEmpty()) {
            // AC-01-3 的句型；好幾個時寫第一個與總數
            const auto& first = result.skipped.constFirst();
            const QString text =
                result.skipped.size() == 1
                    ? tr("無法播放：%1（%2）").arg(displayName(first.path), first.error)
                    : tr("無法播放：%1（%2）等 %3 個項目")
                          .arg(displayName(first.path), first.error)
                          .arg(result.skipped.size());
            m_notifier->notify(text, Notifier::Kind::Error);
        } else if (result.tracks.isEmpty()) {
            m_notifier->notify(tr("沒有可播放的音樂檔：%1").arg(displayName(paths.constFirst())),
                               Notifier::Kind::Error);
        }
    }
    emit opened(static_cast<int>(result.tracks.size()));
}

void OpenFilesController::setInstance(OpenFilesController* instance) {
    g_instance = instance;
}

OpenFilesController* OpenFilesController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "OpenFilesController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui
