#include "library/library_watcher.h"

#include "library/file_enumerator.h"
#include "library/library_manager.h"
#include "library/tag_reader.h"
#include "platform/folder_watcher.h"

#include <auralis/library_query.h>

#include <QLoggingCategory>
#include <QSet>
#include <QTimer>

Q_LOGGING_CATEGORY(lcWatch, "auralis.library.watch")

namespace auralis::core {

namespace {

/// 這批變動會不會改變音樂庫。新增、刪除、改名一律算（資料夾的也算，底下可能有音樂檔）；
/// 「修改」只算音樂檔與 .cue：NTFS 在列舉、讀檔之後會延遲更新資料夾的中繼資料並回報成
/// 資料夾被修改，不濾掉的話每次掃描完都會再觸發一次掃描。空清單（溢位）一律算。
/// 執行緒：監看執行緒。
bool affectsLibrary(const std::vector<FolderWatcher::Change>& changes) {
    if (changes.empty()) {
        return true;
    }
    for (const auto& change : changes) {
        if (change.kind != FolderWatcher::Change::Kind::Modified) {
            return true;
        }
        const qsizetype dot = change.name.lastIndexOf(QLatin1Char('.'));
        const QString extension = dot < 0 ? QString() : change.name.mid(dot + 1).toLower();
        if (extension == QLatin1String("cue") || audioExtensions().contains(extension)) {
            return true;
        }
    }
    return false;
}

} // namespace

struct LibraryWatcher::Watch {
    QString folder;
    QTimer debounce;
    /// 最後宣告：先解構（等監看執行緒結束），之後才不會再有通知
    std::unique_ptr<FolderWatcher> watcher;
};

LibraryWatcher::LibraryWatcher(LibraryManager& manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

LibraryWatcher::~LibraryWatcher() = default;

void LibraryWatcher::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    connect(&m_manager, &LibraryManager::foldersChanged, this, &LibraryWatcher::syncFolders);
    syncFolders();
    if (!m_watches.empty()) {
        m_manager.rescan();
    }
}

QStringList LibraryWatcher::activeFolders() const {
    QStringList folders;
    for (const auto& [key, watch] : m_watches) {
        if (watch->watcher->isActive()) {
            folders.append(watch->folder);
        }
    }
    return folders;
}

void LibraryWatcher::syncFolders() {
    const QStringList folders = m_manager.folders();
    QSet<QString> keys;
    for (const QString& folder : folders) {
        const QString key = pathKey(folder);
        keys.insert(key);
        if (m_watches.count(key) != 0) {
            continue;
        }
        auto watch = std::make_unique<Watch>();
        watch->folder = folder;
        watch->debounce.setSingleShot(true);
        watch->debounce.setInterval(kDebounceMs);
        connect(&watch->debounce, &QTimer::timeout, this,
                [this, folder] { m_manager.rescan(folder); });
        // 監看執行緒的通知一律轉回 UI 執行緒；這個物件解構後送不到的通知由 Qt 丟掉
        watch->watcher = std::make_unique<FolderWatcher>(
            toLongPath(folder),
            [this, key](const std::vector<FolderWatcher::Change>& changes) {
                if (!affectsLibrary(changes)) {
                    return;
                }
                QMetaObject::invokeMethod(
                    this, [this, key] { onChanged(key); }, Qt::QueuedConnection);
            },
            [folder](const QString& error) {
                qCWarning(lcWatch).noquote() << "無法監看音樂資料夾：" << folder << error;
            });
        m_watches.emplace(key, std::move(watch));
    }
    for (auto it = m_watches.begin(); it != m_watches.end();) {
        it = keys.contains(it->first) ? std::next(it) : m_watches.erase(it);
    }
}

void LibraryWatcher::onChanged(const QString& key) {
    const auto found = m_watches.find(key);
    if (found != m_watches.end()) {
        // 每個事件都重新計時：最後一個事件之後 2 秒才掃描
        found->second->debounce.start();
    }
}

} // namespace auralis::core
