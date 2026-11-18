// PlaylistTracks 的排序、移除與加入（US-11 AC-1；畫面規格 S-08）。和載入分開放，
// 讓單一檔案不超過行數上限。
#include "playlist_tracks.h"

#include "playlists_controller.h"

#include <algorithm>

namespace auralis::mod::pl {

int PlaylistTracks::moveRows(const QList<int>& rows, int target) {
    const int n = count();
    std::vector<char> moving(static_cast<size_t>(n), 0);
    for (const int row : rows) {
        if (row >= 0 && row < n) {
            moving[static_cast<size_t>(row)] = 1;
        }
    }
    target = std::clamp(target, 0, n);
    // 新順序：target 前面沒被移動的、被移動的（照原本的順序）、target 之後沒被移動的
    QList<int> order;
    order.reserve(n);
    for (int i = 0; i < target; ++i) {
        if (!moving[static_cast<size_t>(i)]) {
            order.append(i);
        }
    }
    const int first = static_cast<int>(order.size());
    for (int i = 0; i < n; ++i) {
        if (moving[static_cast<size_t>(i)]) {
            order.append(i);
        }
    }
    if (order.size() == first) {
        return -1;
    }
    for (int i = target; i < n; ++i) {
        if (!moving[static_cast<size_t>(i)]) {
            order.append(i);
        }
    }
    // 放回原位置（例：拖到自己的上緣）不寫入
    bool changed = false;
    for (int i = 0; i < n && !changed; ++i) {
        changed = order.at(i) != i;
    }
    return changed && applyOrder(order) ? first : -1;
}

bool PlaylistTracks::moveSelectionBy(int delta) {
    const int n = count();
    QList<int> order(n);
    std::vector<char> selected = m_selected;
    for (int i = 0; i < n; ++i) {
        order[i] = i;
    }
    // 每一列和擋在前面（delta 的方向）的未選取列交換；整段頂到邊的就不動
    if (delta < 0) {
        for (int i = 1; i < n; ++i) {
            if (selected[i] && !selected[i - 1]) {
                std::swap(order[i], order[i - 1]);
                std::swap(selected[i], selected[i - 1]);
            }
        }
    } else {
        for (int i = n - 2; i >= 0; --i) {
            if (selected[i] && !selected[i + 1]) {
                std::swap(order[i], order[i + 1]);
                std::swap(selected[i], selected[i + 1]);
            }
        }
    }
    bool changed = false;
    for (int i = 0; i < n && !changed; ++i) {
        changed = order[i] != i;
    }
    return changed && applyOrder(order);
}

bool PlaylistTracks::applyOrder(const QList<int>& order) {
    m_selfChange = true;
    const bool written = m_playlists.reorder(m_playlistId, order);
    m_selfChange = false;
    if (!written) {
        return false;
    }
    // 只換順序、列數不變：用 layoutChanged（VerticalSortHint）讓清單只更新內容，
    // 目前列與捲動位置都留著（重設模型會跳回最上面）
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    QList<Row> rows;
    std::vector<char> selected(m_selected.size(), 0);
    rows.reserve(order.size());
    for (int i = 0; i < order.size(); ++i) {
        Row row = m_rows.at(order.at(i));
        row.item.pos = i;
        rows.append(row);
        selected[static_cast<size_t>(i)] = m_selected[static_cast<size_t>(order.at(i))];
    }
    QModelIndexList from = persistentIndexList();
    QModelIndexList to;
    for (const QModelIndex& index : from) {
        to.append(this->index(static_cast<int>(order.indexOf(index.row()))));
    }
    changePersistentIndexList(from, to);
    m_rows = rows;
    m_selected = selected;
    // 錨點跟著那一列走（排序後接著 Shift＋上下鍵複選）
    m_anchor = m_anchor >= 0 ? static_cast<int>(order.indexOf(m_anchor)) : -1;
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    selectionDone();
    return true;
}

bool PlaylistTracks::removeSelected() {
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return false;
    }
    m_selfChange = true;
    const bool removed = m_playlists.removeItems(m_playlistId, rows);
    m_selfChange = false;
    if (!removed) {
        return false;
    }
    // 從後面往前一段一段拿掉，留下的列不重建
    int end = static_cast<int>(rows.size()) - 1;
    while (end >= 0) {
        int start = end;
        while (start > 0 && rows.at(start - 1) == rows.at(start) - 1) {
            --start;
        }
        beginRemoveRows({}, rows.at(start), rows.at(end));
        m_rows.remove(rows.at(start), rows.at(end) - rows.at(start) + 1);
        m_selected.erase(m_selected.begin() + rows.at(start),
                         m_selected.begin() + rows.at(end) + 1);
        endRemoveRows();
        end = start - 1;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].item.pos = i;
    }
    m_selectedCount = 0;
    m_anchor = -1;
    selectionDone();
    emit summaryChanged();
    return true;
}

bool PlaylistTracks::insertTracks(const QString& payloadJson, int row) {
    return m_playlistId > 0 && m_playlists.addTracks(m_playlistId, payloadJson, row);
}

} // namespace auralis::mod::pl
