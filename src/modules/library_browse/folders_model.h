#pragma once

#include "folder_tree.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace auralis::mod::lib {

/// 資料夾頁（S-06）左欄的樹，攤平成看得到的列（收合的子樹不在列裡），給 ListView 用。
/// 角色：folderId、name、path、depth、hasChildren、expanded、missing、root。
/// 展開與收合用插入與移除通知，其他列不重建。展開狀態以資料夾 id 記住，重讀後保留；
/// 根第一次出現時預設展開一層（含掃描後才出現的）。執行緒：只在 UI 執行緒使用。
class FoldersModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 LibraryFolders.tree 提供")

public:
    enum Role {
        FolderIdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
        MissingRole,
        RootRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 換成新讀到的樹（整個重設；展開狀態依 id 保留）。
    void setNodes(QList<FolderNode> nodes);
    const QList<FolderNode>& nodes() const { return m_nodes; }

    /// 第 row 列在 nodes() 的位置；不在範圍時 -1。
    int nodeIndex(int row) const;
    /// 資料夾 id 所在的列（祖先都展開才看得到）；看不到時 -1。
    int rowOfFolder(qint64 folderId) const;
    /// 父資料夾的列；根或不在範圍時 -1。
    int parentRow(int row) const;

    /// 展開或收合第 row 列；沒有子資料夾、已經是那個狀態或不在範圍時回傳 false。
    bool expand(int row);
    bool collapse(int row);

private:
    /// node 的子樹裡依展開狀態看得到的節點（不含 node 自己）。
    QList<int> visibleChildren(int node) const;

    QList<FolderNode> m_nodes;
    QList<int> m_rows; ///< 看得到的列 → m_nodes 的位置
    QSet<qint64> m_expanded;
    QSet<qint64> m_seenRoots; ///< 出現過的根（只有第一次出現時自動展開）
};

} // namespace auralis::mod::lib
