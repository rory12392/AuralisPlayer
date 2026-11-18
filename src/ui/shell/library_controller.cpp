#include "shell/library_controller.h"

#include "library/library_manager.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJSEngine>
#include <QLocale>
#include <QQmlEngine>

#include <algorithm>

namespace auralis::ui {

namespace {

LibraryController* g_instance = nullptr;

using AddResult = core::LibraryManager::AddResult;

/// 數量用千分位（介面規範 §6.1：40,118）。
QString grouped(int value) {
    return QLocale(QLocale::English).toString(value);
}

} // namespace

LibraryController::LibraryController(core::LibraryManager& manager, const LibraryQuery& query,
                                     Dialogs* dialogs, Notifier* notifier, QObject* parent)
    : QObject(parent), m_manager(manager), m_query(query), m_dialogs(dialogs),
      m_notifier(notifier) {
    // 曲目數一次查一份就好
    m_pool.setMaxThreadCount(1);
    connect(&m_manager, &core::LibraryManager::foldersChanged, this, [this] {
        // 剛加入的資料夾被移除了：歡迎畫面回到初始狀態
        const QString key = pathKey(m_addedPath);
        const QStringList current = m_manager.folders();
        if (!m_addedPath.isEmpty() &&
            std::none_of(current.cbegin(), current.cend(),
                         [&key](const QString& folder) { return pathKey(folder) == key; })) {
            m_addedPath.clear();
            emit addedPathChanged();
        }
        emit foldersChanged();
    });
    connect(&m_manager, &core::LibraryManager::scanningChanged, this,
            &LibraryController::scanningChanged);
    connect(&m_manager, &core::LibraryManager::progressChanged, this,
            &LibraryController::progressChanged);
    connect(&m_manager, &core::LibraryManager::lastReportChanged, this,
            &LibraryController::reportChanged);
    connect(&m_manager, &core::LibraryManager::operationFailed, this,
            [this] { notify(tr("音樂庫更新失敗，詳細原因已寫進日誌。"), true); });
    // 加入、移除、掃描做完（含取消）都會發；這時重查每個資料夾的曲目數
    connect(&m_query, &LibraryQuery::scanFinished, this, &LibraryController::refreshCounts);
    refreshCounts();
}

LibraryController::~LibraryController() {
    m_pool.waitForDone();
}

QVariantList LibraryController::folders() const {
    QVariantList rows;
    for (const QString& path : m_manager.folders()) {
        rows.append(QVariantMap{{QStringLiteral("path"), path},
                                {QStringLiteral("trackCount"),
                                 m_countsLoaded ? m_counts.value(pathKey(path), 0) : -1}});
    }
    return rows;
}

bool LibraryController::hasFolders() const {
    return !m_manager.folders().isEmpty();
}

bool LibraryController::scanning() const {
    return m_manager.isScanning();
}

int LibraryController::scannedCount() const {
    return m_manager.scannedCount();
}

int LibraryController::totalCount() const {
    return m_manager.totalCount();
}

qreal LibraryController::progress() const {
    const int total = m_manager.totalCount();
    return total > 0 ? qreal(m_manager.scannedCount()) / total : 0;
}

QString LibraryController::progressText() const {
    return formatProgress(m_manager.scannedCount(), m_manager.totalCount());
}

QString LibraryController::formatProgress(int scanned, int total) {
    if (total <= 0) {
        return {};
    }
    return QStringLiteral("%1 / %2").arg(grouped(scanned), grouped(total));
}

QVariantMap LibraryController::report() const {
    const core::ScanReport last = m_manager.lastReport();
    QVariantList failures;
    for (const auto& failure : last.failures) {
        failures.append(QVariantMap{{QStringLiteral("path"), failure.path},
                                    {QStringLiteral("reason"), failureReason(failure.error)}});
    }
    // 耗時：一分鐘內寫到 0.1 秒，之後 m:ss（介面規範 §6.1 的時間寫法）
    const qint64 ms = qMax<qint64>(0, last.finishedAt - last.startedAt);
    const bool underMinute = ms < 60000;
    const QString duration = underMinute ? QString::number(ms / 1000.0, 'f', 1)
                                         : QStringLiteral("%1:%2")
                                               .arg(ms / 60000)
                                               .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'));
    return {{QStringLiteral("hasReport"), last.id != 0},
            {QStringLiteral("tracks"), qMax(0, m_trackCount)},
            {QStringLiteral("added"), last.added},
            {QStringLiteral("removed"), last.removed},
            {QStringLiteral("failed"), last.failed},
            {QStringLiteral("cancelled"), last.cancelled},
            {QStringLiteral("durationValue"), last.id != 0 ? duration : QString()},
            {QStringLiteral("durationUnit"), underMinute ? tr("秒") : QString()},
            {QStringLiteral("finishedText"), last.id != 0
                                                 ? QDateTime::fromMSecsSinceEpoch(last.finishedAt)
                                                       .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                                 : QString()},
            {QStringLiteral("failures"), failures}};
}

QString LibraryController::failureReason(const QString& error) {
    // 拒絕存取（0x80070005）與共用違規（0x80070020）：使用者能做的都是檢查權限或關掉佔用的程式
    if (error.contains(QLatin1String("0x80070005")) ||
        error.contains(QLatin1String("0x80070020"))) {
        return tr("沒有讀取權限");
    }
    if (error.startsWith(QStringLiteral("無法解析檔案"))) {
        return tr("檔案損毀");
    }
    // 其他系統錯誤不給使用者看錯誤碼（介面規範 §6.1），細節在日誌與資料庫
    if (error.contains(QLatin1String("HRESULT"))) {
        return tr("無法讀取");
    }
    return error;
}

QString LibraryController::failuresText() const {
    QStringList lines;
    for (const auto& failure : m_manager.lastReport().failures) {
        lines << failure.path + QLatin1Char('\t') + failureReason(failure.error);
    }
    return lines.join(QLatin1Char('\n'));
}

void LibraryController::chooseFolder() {
    if (m_dialogs == nullptr) {
        return;
    }
    m_dialogs->chooseFolder(tr("加入音樂資料夾"), [this](const QStringList& paths) {
        if (!paths.isEmpty()) {
            addFolder(paths.constFirst());
        }
    });
}

bool LibraryController::addFolder(const QString& path) {
    const QString folder = QDir::toNativeSeparators(path);
    switch (m_manager.addFolder(folder)) {
    case AddResult::Added:
        m_addedPath = folder;
        emit addedPathChanged();
        return true;
    case AddResult::AlreadyAdded:
        notify(tr("%1 已經在音樂庫裡。").arg(folder), false);
        return false;
    case AddResult::Overlaps:
        notify(tr("%1 和已加入的音樂資料夾互相包含，請加入其中一個就好。").arg(folder), true);
        return false;
    case AddResult::NotFound:
        break;
    }
    notify(tr("無法讀取 %1，請確認資料夾存在而且有權限。").arg(folder), true);
    return false;
}

void LibraryController::confirmRemove(const QString& path) {
    if (m_dialogs == nullptr) {
        return;
    }
    m_dialogs->confirm({tr("移除音樂資料夾 %1").arg(path),
                        tr("這個資料夾的曲目會從音樂庫消失，音樂檔不會被刪除。"), tr("移除資料夾"),
                        true},
                       [this, path](bool confirmed) {
                           if (confirmed) {
                               m_manager.removeFolder(path);
                           }
                       });
}

void LibraryController::rescan() {
    m_manager.rescan();
}

void LibraryController::cancelScan() {
    m_manager.cancelScan();
}

void LibraryController::copyFailures() {
    QGuiApplication::clipboard()->setText(failuresText());
    // 剪貼簿看不到，用通知列說一聲（介面規範 §6「操作成功」）
    notify(tr("已複製 %1 個讀不到的檔案").arg(m_manager.lastReport().failures.size()), false);
}

void LibraryController::refreshCounts() {
    const QStringList roots = m_manager.folders();
    m_pool.start([this, roots, &query = m_query] {
        // 每首曲目算進包含它的那個音樂資料夾（音樂資料夾不會互相包含）
        QHash<QString, int> counts;
        QStringList keys;
        for (const QString& root : roots) {
            counts.insert(pathKey(root), 0);
            // 磁碟根目錄（E:\）本身就以分隔符結尾
            const QString key = pathKey(root);
            keys << (key.endsWith(QLatin1Char('\\')) ? key : key + QLatin1Char('\\'));
        }
        int total = 0;
        const auto tracks = query.tracks({});
        if (tracks) {
            total = static_cast<int>(tracks.value().size());
            for (const TrackInfo& track : tracks.value()) {
                const QString key = pathKey(track.ref.path);
                for (int i = 0; i < keys.size(); ++i) {
                    if (key.startsWith(keys.at(i))) {
                        ++counts[pathKey(roots.at(i))];
                        break;
                    }
                }
            }
        }
        QMetaObject::invokeMethod(
            this, [this, counts, total] { applyCounts(counts, total); }, Qt::QueuedConnection);
    });
}

void LibraryController::applyCounts(const QHash<QString, int>& counts, int total) {
    m_counts = counts;
    m_countsLoaded = true;
    m_trackCount = total;
    emit foldersChanged();
    emit reportChanged();
}

void LibraryController::notify(const QString& text, bool error) {
    if (m_notifier != nullptr) {
        m_notifier->notify(text, error ? Notifier::Kind::Error : Notifier::Kind::Info);
    }
}

void LibraryController::waitForCounts() {
    m_pool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

void LibraryController::setInstance(LibraryController* instance) {
    g_instance = instance;
}

LibraryController* LibraryController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "LibraryController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui
