#include "folders_controller.h"

#include <auralis/library_query.h>
#include <auralis/module_settings.h>
#include <auralis/playback_service.h>

#include <QJSEngine>
#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcLib)

namespace auralis::mod::lib {

namespace {

FoldersController* g_instance = nullptr;

const QString kPaneWidthKey = QStringLiteral("foldersPaneWidth");

int clampPaneWidth(int width) {
    return std::clamp(width, FoldersController::kMinPaneWidth, FoldersController::kMaxPaneWidth);
}

} // namespace

FoldersController::FoldersController(LibraryQuery& library, PlaybackService& playback,
                                     ModuleSettings* settings, bool checkDisk, QObject* parent)
    : QObject(parent), m_library(library), m_playback(playback), m_settings(settings),
      m_checkDisk(checkDisk) {
    m_pool.setMaxThreadCount(1);
    if (m_settings && m_settings->contains(kPaneWidthKey)) {
        m_paneWidth = clampPaneWidth(m_settings->value(kPaneWidthKey).toInt());
    }
    connect(&m_library, &LibraryQuery::scanFinished, this, [this] {
        if (m_loaded || m_busy) {
            reload();
        }
    });
}

FoldersController::~FoldersController() {
    shutdown();
}

void FoldersController::shutdown() {
    ++*m_treeGeneration;
    ++*m_tracksGeneration;
    m_pool.clear();
    m_pool.waitForDone();
}

const FolderNode* FoldersController::selectedNode() const {
    const int node = m_tree.nodeIndex(selectedRow());
    return node < 0 ? nullptr : &m_tree.nodes().at(node);
}

QString FoldersController::selectedPath() const {
    const FolderNode* node = selectedNode();
    return node ? node->info.path : QString();
}

bool FoldersController::selectedMissing() const {
    const FolderNode* node = selectedNode();
    return node && node->missing;
}

void FoldersController::setPaneWidth(int width) {
    const int value = clampPaneWidth(width);
    if (value == m_paneWidth) {
        return;
    }
    m_paneWidth = value;
    if (m_settings) {
        m_settings->setValue(kPaneWidthKey, m_paneWidth);
    }
    emit paneWidthChanged();
}

void FoldersController::load() {
    if (!m_loaded && !m_busy) {
        reload();
    }
}

void FoldersController::reload() {
    if (m_busy) {
        m_reloadPending = true;
        return;
    }
    m_busy = true;
    m_reloadPending = false;
    emit changed();
    const quint64 generation = ++*m_treeGeneration;
    const bool checkDisk = m_checkDisk;
    m_pool.start([this, generation, checkDisk] {
        if (generation != *m_treeGeneration) {
            return;
        }
        QList<FolderNode> nodes;
        const QString error = readFolderTree(m_library, nodes, checkDisk);
        if (generation != *m_treeGeneration) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, nodes = std::move(nodes), error]() mutable {
                onTreeLoaded(generation, std::move(nodes), error);
            },
            Qt::QueuedConnection);
    });
}

void FoldersController::onTreeLoaded(quint64 generation, QList<FolderNode> nodes,
                                     const QString& error) {
    if (generation != *m_treeGeneration) {
        return;
    }
    m_busy = false;
    m_error = error;
    if (error.isEmpty()) {
        m_tree.setNodes(std::move(nodes));
        m_loaded = true;
        if (m_tree.rowOfFolder(m_selectedId) < 0) {
            // 第一次或所選的資料夾不在了（或被收合在看不到的地方）：選第一個音樂資料夾
            m_selectedId =
                m_tree.rowCount() > 0 ? m_tree.nodes().at(m_tree.nodeIndex(0)).info.id : 0;
        }
        emit selectionChanged();
        reloadTracks();
    } else {
        qCWarning(lcLib) << "資料夾樹讀取失敗：" << error;
    }
    emit changed();
    if (m_reloadPending) {
        reload();
    }
}

void FoldersController::select(int row) {
    const int node = m_tree.nodeIndex(row);
    if (node < 0) {
        return;
    }
    const qint64 id = m_tree.nodes().at(node).info.id;
    if (id == m_selectedId) {
        return;
    }
    m_selectedId = id;
    emit selectionChanged();
    reloadTracks();
}

void FoldersController::reloadTracks() {
    const int node = m_tree.nodeIndex(selectedRow());
    const quint64 generation = ++*m_tracksGeneration;
    m_tracksLoaded = false;
    m_tracksError.clear();
    m_tracks.reset({});
    emit tracksChanged();
    if (node < 0) {
        m_tracksLoaded = true;
        emit tracksChanged();
        return;
    }
    const QList<qint64> ids = subtreeFolderIds(m_tree.nodes(), node);
    m_pool.start([this, generation, ids] {
        if (generation != *m_tracksGeneration) {
            return; // 已經換了選取，還沒開始的讀取直接丟掉
        }
        QList<TrackInfo> tracks;
        const QString error = readFolderTracks(m_library, ids, tracks);
        if (generation != *m_tracksGeneration) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, tracks = std::move(tracks), error]() mutable {
                onTracksLoaded(generation, std::move(tracks), error);
            },
            Qt::QueuedConnection);
    });
}

void FoldersController::onTracksLoaded(quint64 generation, QList<TrackInfo> tracks,
                                       const QString& error) {
    if (generation != *m_tracksGeneration) {
        return;
    }
    m_tracksLoaded = true;
    m_tracksError = error;
    if (error.isEmpty()) {
        m_tracks.reset(std::move(tracks));
    } else {
        qCWarning(lcLib) << "資料夾的曲目讀取失敗：" << error;
    }
    emit tracksChanged();
}

bool FoldersController::expand(int row) {
    const bool done = m_tree.expand(row);
    if (done) {
        emit selectionChanged(); // 插入的列在選取的前面時列號跟著變
    }
    return done;
}

bool FoldersController::collapse(int row) {
    const bool done = m_tree.collapse(row);
    if (done) {
        if (m_selectedId != 0 && m_tree.rowOfFolder(m_selectedId) < 0) {
            select(row); // 選取的資料夾被收進去了：改選收合的這個資料夾
        } else {
            emit selectionChanged();
        }
    }
    return done;
}

bool FoldersController::playFrom(int row) {
    const int count = m_tracks.rowCount();
    if (row < 0 || row >= count) {
        return false;
    }
    QList<TrackRef> tracks;
    tracks.reserve(count);
    for (int i = 0; i < count; ++i) {
        tracks.append(m_tracks.track(i).ref);
    }
    m_playback.playFrom(tracks, row);
    return true;
}

void FoldersController::setInstance(FoldersController* instance) {
    g_instance = instance;
}

FoldersController* FoldersController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "FoldersController", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib
