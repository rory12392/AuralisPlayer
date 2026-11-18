#pragma once

#include <auralis/library_query.h>

#include <QList>
#include <QString>

namespace auralis::mod::lib {

/// 資料夾頁（S-06）樹的一個節點。整棵樹依深度優先前序排成一列（根依名稱、子資料夾依名稱，
/// 也就是 LibraryQuery::folders 的順序），所以一個節點的子樹是連續的一段 [自己, subtreeEnd)。
struct FolderNode {
    FolderInfo info;
    int depth = 0;        ///< 根（使用者加入的音樂資料夾）是 0
    int parent = -1;      ///< 父節點在清單裡的位置；根是 -1
    int subtreeEnd = 0;   ///< 子樹之後的第一個位置
    bool missing = false; ///< 磁碟上找不到這個資料夾（列標「找不到資料夾」）
};

/// index 的節點有沒有子資料夾。
inline bool folderHasChildren(const QList<FolderNode>& nodes, int index) {
    return nodes.at(index).subtreeEnd > index + 1;
}

/// 讀整棵資料夾樹（LibraryQuery::folders 從根一層一層往下，結構與 folders 表一致，AC-10-3）。
/// checkDisk 為 true 時另外檢查每個資料夾在磁碟上還在不在（只查存在，不讀內容）。
/// 失敗時回傳錯誤訊息，成功時空字串。在工作執行緒呼叫；例外也轉成錯誤訊息（錯誤邊界）。
QString readFolderTree(const LibraryQuery& library, QList<FolderNode>& out, bool checkDisk);

/// 子樹的資料夾 id（含自己），依前序。
QList<qint64> subtreeFolderIds(const QList<FolderNode>& nodes, int index);

/// 子樹不超過這麼多個資料夾時逐一查（TrackFilter::folderId）；超過時讀全部曲目再挑出來。
constexpr int kPerFolderQueryLimit = 32;

/// 資料夾（含子資料夾）的曲目：依 folderIds 的順序（樹的前序）一個資料夾接一個，
/// 資料夾內照 LibraryQuery::tracks 的順序。失敗時回傳錯誤訊息。在工作執行緒呼叫。
QString readFolderTracks(const LibraryQuery& library, const QList<qint64>& folderIds,
                         QList<TrackInfo>& out);

} // namespace auralis::mod::lib
