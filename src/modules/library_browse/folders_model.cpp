#include "folders_model.h"

namespace auralis::mod::lib {

int FoldersModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant FoldersModel::data(const QModelIndex& index, int role) const {
    const int node = nodeIndex(index.isValid() ? index.row() : -1);
    if (node < 0) {
        return {};
    }
    const FolderNode& n = m_nodes.at(node);
    switch (role) {
    case FolderIdRole:
        return n.info.id;
    case Qt::DisplayRole:
    case NameRole:
        return n.info.name;
    case PathRole:
        return n.info.path;
    case DepthRole:
        return n.depth;
    case HasChildrenRole:
        return folderHasChildren(m_nodes, node);
    case ExpandedRole:
        return m_expanded.contains(n.info.id);
    case MissingRole:
        return n.missing;
    case RootRole:
        return n.parent < 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> FoldersModel::roleNames() const {
    return {{FolderIdRole, "folderId"},
            {NameRole, "name"},
            {PathRole, "path"},
            {DepthRole, "depth"},
            {HasChildrenRole, "hasChildren"},
            {ExpandedRole, "expanded"},
            {MissingRole, "missing"},
            {RootRole, "root"}};
}

QList<int> FoldersModel::visibleChildren(int node) const {
    QList<int> rows;
    const int end = m_nodes.at(node).subtreeEnd;
    int i = node + 1;
    while (i < end) {
        rows.append(i);
        const FolderNode& n = m_nodes.at(i);
        // 收合的資料夾跳過它的子樹
        i = m_expanded.contains(n.info.id) ? i + 1 : n.subtreeEnd;
    }
    return rows;
}

void FoldersModel::setNodes(QList<FolderNode> nodes) {
    beginResetModel();
    m_nodes = std::move(nodes);
    // 第一次出現的根（含第一次讀到時音樂庫還是空的、掃描後才加入的）預設展開一層；
    // 之後使用者收合了就照使用者的
    for (const FolderNode& n : std::as_const(m_nodes)) {
        if (n.parent < 0 && !m_seenRoots.contains(n.info.id)) {
            m_seenRoots.insert(n.info.id);
            m_expanded.insert(n.info.id);
        }
    }
    m_rows.clear();
    int i = 0;
    while (i < m_nodes.size()) {
        m_rows.append(i);
        const FolderNode& n = m_nodes.at(i);
        i = m_expanded.contains(n.info.id) ? i + 1 : n.subtreeEnd;
    }
    endResetModel();
}

int FoldersModel::nodeIndex(int row) const {
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : -1;
}

int FoldersModel::rowOfFolder(qint64 folderId) const {
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_nodes.at(m_rows.at(row)).info.id == folderId) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int FoldersModel::parentRow(int row) const {
    const int node = nodeIndex(row);
    return node < 0 ? -1 : static_cast<int>(m_rows.indexOf(m_nodes.at(node).parent));
}

bool FoldersModel::expand(int row) {
    const int node = nodeIndex(row);
    if (node < 0 || !folderHasChildren(m_nodes, node) ||
        m_expanded.contains(m_nodes.at(node).info.id)) {
        return false;
    }
    m_expanded.insert(m_nodes.at(node).info.id);
    const QList<int> rows = visibleChildren(node);
    beginInsertRows({}, row + 1, row + static_cast<int>(rows.size()));
    for (qsizetype k = 0; k < rows.size(); ++k) {
        m_rows.insert(row + 1 + k, rows.at(k));
    }
    endInsertRows();
    emit dataChanged(index(row), index(row), {ExpandedRole});
    return true;
}

bool FoldersModel::collapse(int row) {
    const int node = nodeIndex(row);
    if (node < 0 || !folderHasChildren(m_nodes, node) ||
        !m_expanded.contains(m_nodes.at(node).info.id)) {
        return false;
    }
    // 看得到的子孫是緊接在後面、節點位置落在子樹範圍內的那幾列
    const int end = m_nodes.at(node).subtreeEnd;
    int last = row;
    while (last + 1 < m_rows.size() && m_rows.at(last + 1) < end) {
        ++last;
    }
    m_expanded.remove(m_nodes.at(node).info.id);
    if (last > row) {
        beginRemoveRows({}, row + 1, last);
        m_rows.remove(row + 1, last - row);
        endRemoveRows();
    }
    emit dataChanged(index(row), index(row), {ExpandedRole});
    return true;
}

} // namespace auralis::mod::lib
