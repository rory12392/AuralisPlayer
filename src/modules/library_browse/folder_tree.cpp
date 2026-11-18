#include "folder_tree.h"

#include <QFileInfo>
#include <QHash>

#include <exception>

namespace auralis::mod::lib {

namespace {

/// 把 parentId 的子資料夾（與它們的子樹）依前序接在 out 後面。
QString appendChildren(const LibraryQuery& library, qint64 parentId, int parentIndex, int depth,
                       QList<FolderNode>& out) {
    const auto children = library.folders(parentId);
    if (!children) {
        return children.error().message;
    }
    for (const FolderInfo& info : children.value()) {
        const int index = static_cast<int>(out.size());
        FolderNode node;
        node.info = info;
        node.depth = depth;
        node.parent = parentIndex;
        out.append(std::move(node));
        const QString error = appendChildren(library, info.id, index, depth + 1, out);
        if (!error.isEmpty()) {
            return error;
        }
        out[index].subtreeEnd = static_cast<int>(out.size());
    }
    return {};
}

} // namespace

QString readFolderTree(const LibraryQuery& library, QList<FolderNode>& out, bool checkDisk) {
    try {
        QList<FolderNode> nodes;
        const QString error = appendChildren(library, 0, -1, 0, nodes);
        if (!error.isEmpty()) {
            return error;
        }
        if (checkDisk) {
            for (FolderNode& node : nodes) {
                node.missing = !QFileInfo(node.info.path).isDir();
            }
        }
        out = std::move(nodes);
        return {};
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    }
}

QList<qint64> subtreeFolderIds(const QList<FolderNode>& nodes, int index) {
    QList<qint64> ids;
    if (index < 0 || index >= nodes.size()) {
        return ids;
    }
    for (int i = index; i < nodes.at(index).subtreeEnd; ++i) {
        ids.append(nodes.at(i).info.id);
    }
    return ids;
}

QString readFolderTracks(const LibraryQuery& library, const QList<qint64>& folderIds,
                         QList<TrackInfo>& out) {
    try {
        QList<TrackInfo> tracks;
        if (folderIds.size() <= kPerFolderQueryLimit) {
            for (const qint64 id : folderIds) {
                TrackFilter filter;
                filter.folderId = id;
                const auto part = library.tracks(filter);
                if (!part) {
                    return part.error().message;
                }
                tracks.append(part.value());
            }
        } else {
            // 大的子樹（例：整個音樂資料夾）：一次讀全部曲目，依資料夾分好再照樹的順序接起來
            const auto all = library.tracks(TrackFilter{});
            if (!all) {
                return all.error().message;
            }
            QHash<qint64, QList<TrackInfo>> byFolder;
            byFolder.reserve(folderIds.size());
            for (const qint64 id : folderIds) {
                byFolder.insert(id, {});
            }
            for (const TrackInfo& track : all.value()) {
                const auto it = byFolder.find(track.folderId);
                if (it != byFolder.end()) {
                    it->append(track);
                }
            }
            for (const qint64 id : folderIds) {
                tracks.append(byFolder.value(id));
            }
        }
        out = std::move(tracks);
        return {};
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    }
}

} // namespace auralis::mod::lib
