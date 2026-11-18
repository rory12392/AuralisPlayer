// PlaylistTracks 的選取與它代表的曲目（介面規範 §7 清單的鍵盤操作）。和載入、排序分開放，
// 讓單一檔案不超過行數上限。
#include "playlist_tracks.h"

#include "playlist_parse.h"

#include <auralis/track_drag_payload.h>

#include <QGuiApplication>
#include <QVariantMap>

#include <algorithm>

namespace auralis::mod::pl {

bool PlaylistTracks::isSelected(int row) const {
    return row >= 0 && row < static_cast<int>(m_selected.size()) &&
           m_selected[static_cast<size_t>(row)] != 0;
}

void PlaylistTracks::select(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    std::fill(m_selected.begin(), m_selected.end(), 0);
    m_selected[static_cast<size_t>(row)] = 1;
    m_anchor = row;
    selectionDone();
}

void PlaylistTracks::toggle(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    char& flag = m_selected[static_cast<size_t>(row)];
    flag = flag != 0 ? 0 : 1;
    m_anchor = row;
    selectionDone();
}

void PlaylistTracks::extend(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    if (m_anchor < 0 || m_anchor >= count()) {
        select(row);
        return;
    }
    std::fill(m_selected.begin(), m_selected.end(), 0);
    const int from = std::min(m_anchor, row);
    const int to = std::max(m_anchor, row);
    std::fill(m_selected.begin() + from, m_selected.begin() + to + 1, 1);
    selectionDone();
}

void PlaylistTracks::selectAll() {
    std::fill(m_selected.begin(), m_selected.end(), 1);
    selectionDone();
}

void PlaylistTracks::clearSelection() {
    std::fill(m_selected.begin(), m_selected.end(), 0);
    m_anchor = -1;
    selectionDone();
}

void PlaylistTracks::click(int row) {
    const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    if (modifiers & Qt::ControlModifier) {
        toggle(row);
    } else if (modifiers & Qt::ShiftModifier) {
        extend(row);
    } else {
        select(row);
    }
}

void PlaylistTracks::ensureSelected(int row) {
    if (!isSelected(row)) {
        select(row);
    }
}

QList<int> PlaylistTracks::selectedRows() const {
    QList<int> rows;
    for (int i = 0; i < static_cast<int>(m_selected.size()); ++i) {
        if (m_selected[static_cast<size_t>(i)] != 0) {
            rows.append(i);
        }
    }
    return rows;
}

QVariantList PlaylistTracks::selectedTracks() const {
    QVariantList tracks;
    for (const int row : selectedRows()) {
        const TrackRef& ref = m_rows.at(row).item.track;
        // 找不到的項目（被拒絕的路徑、經過連結的路徑）不交給右鍵選單（接著播放、加到佇列）
        if (blockedFromOpening(m_rows.at(row).item.missing)) {
            continue;
        }
        tracks.append(QVariantMap{{QStringLiteral("path"), ref.path},
                                  {QStringLiteral("subsong"), static_cast<int>(ref.subsong)}});
    }
    return tracks;
}

QString PlaylistTracks::payloadJson() const {
    // 播放清單沒有查詢條件可以代表，所以一律列出曲目（4 萬首的 JSON 在 C++ 組，不經 QML 陣列）
    TrackDragPayload payload;
    for (const int row : selectedRows()) {
        // 找不到的項目（被拒絕的路徑、經過連結的路徑）不拖出去（佇列面板會開啟它）
        const PlaylistItem& item = m_rows.at(row).item;
        if (!blockedFromOpening(item.missing)) {
            payload.tracks.append(item.track);
        }
    }
    return QString::fromUtf8(payload.toJson());
}

void PlaylistTracks::selectionDone() {
    m_selectedCount = static_cast<int>(std::count(m_selected.begin(), m_selected.end(), 1));
    ++m_revision;
    emit selectionChanged();
}

} // namespace auralis::mod::pl
