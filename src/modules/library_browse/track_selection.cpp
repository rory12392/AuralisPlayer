#include "track_selection.h"

#include "albums_model.h"
#include "search_criteria.h"
#include "search_results_model.h"

#include <auralis/track_drag_payload.h>

#include <QGuiApplication>
#include <QVariantMap>

#include <algorithm>

namespace auralis::mod::lib {

namespace {

TrackInfo g_hint; // 最近一次只有一首的右鍵選單內容（「前往專輯」「前往藝人」用）

QVariantMap trackRef(const TrackInfo& track) {
    return {{QStringLiteral("path"), track.ref.path},
            {QStringLiteral("subsong"), track.ref.subsong}};
}

} // namespace

TrackSelection::TrackSelection(QObject* parent) : QObject(parent) {}

void TrackSelection::setModel(QAbstractItemModel* model) {
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        const auto reset = [this] {
            clear();
        };
        connect(m_model, &QAbstractItemModel::modelReset, this, reset);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, reset);
        connect(m_model, &QAbstractItemModel::rowsMoved, this, reset);
        connect(m_model, &QAbstractItemModel::layoutChanged, this, reset);
        connect(m_model, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex&, int first, int) {
                    // 只在最後接上（搜尋分批載入）時保留選取；全選時新列也算選取
                    if (first >= static_cast<int>(m_selected.size())) {
                        resize(m_all);
                    } else {
                        clear();
                    }
                });
        connect(m_model, &QAbstractItemModel::dataChanged, this, [this] {
            if (m_count > 0) {
                finish(); // 專輯的曲數可能變了
            }
        });
    }
    clear();
    emit modelChanged();
}

void TrackSelection::setAllQuery(const QString& query) {
    if (m_allQuery == query) {
        return;
    }
    m_allQuery = query;
    emit allQueryChanged();
    emit changed();
}

void TrackSelection::setComplete(bool complete) {
    if (m_complete == complete) {
        return;
    }
    m_complete = complete;
    emit changed();
}

int TrackSelection::rowCount() const {
    return m_model ? m_model->rowCount() : 0;
}

bool TrackSelection::queryOnly() const {
    return m_trackCount > TrackDragPayload::kMaxTracks && !query().isEmpty();
}

bool TrackSelection::isSelected(int row) const {
    return row >= 0 && row < static_cast<int>(m_selected.size()) && m_selected[row] != 0;
}

void TrackSelection::resize(bool keepAll) {
    const auto oldSize = static_cast<int>(m_selected.size());
    const int rows = rowCount();
    m_selected.resize(rows, 0);
    if (keepAll && rows > oldSize) {
        std::fill(m_selected.begin() + oldSize, m_selected.end(), 1);
    }
    finish();
}

void TrackSelection::setRange(int from, int to) {
    std::fill(m_selected.begin(), m_selected.end(), 0);
    if (from > to) {
        std::swap(from, to);
    }
    for (int row = std::max(0, from); row <= to && row < static_cast<int>(m_selected.size());
         ++row) {
        m_selected[row] = 1;
    }
}

void TrackSelection::finish() {
    m_count = static_cast<int>(std::count(m_selected.begin(), m_selected.end(), 1));
    m_trackCount = 0;
    for (int row = 0; row < static_cast<int>(m_selected.size()); ++row) {
        if (m_selected[row] != 0) {
            m_trackCount += rowTrackCount(row);
        }
    }
    ++m_revision;
    emit changed();
}

void TrackSelection::select(int row) {
    m_selected.assign(rowCount(), 0);
    m_all = false;
    if (row < 0 || row >= static_cast<int>(m_selected.size())) {
        m_anchor = -1;
    } else {
        m_selected[row] = 1;
        m_anchor = row;
    }
    finish();
}

void TrackSelection::toggle(int row) {
    m_selected.resize(rowCount(), 0);
    if (row < 0 || row >= static_cast<int>(m_selected.size())) {
        return;
    }
    m_selected[row] = m_selected[row] != 0 ? 0 : 1;
    m_anchor = row;
    m_all = false;
    finish();
}

void TrackSelection::extend(int row) {
    m_selected.resize(rowCount(), 0);
    if (m_anchor < 0 || m_anchor >= static_cast<int>(m_selected.size())) {
        select(row);
        return;
    }
    m_all = false;
    setRange(m_anchor, row);
    finish();
}

void TrackSelection::selectAll() {
    m_selected.assign(rowCount(), 1);
    m_all = true;
    finish();
}

void TrackSelection::clear() {
    m_selected.assign(rowCount(), 0);
    m_anchor = -1;
    m_all = false;
    finish();
}

void TrackSelection::click(int row) {
    const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
    if (modifiers & Qt::ControlModifier) {
        toggle(row);
    } else if (modifiers & Qt::ShiftModifier) {
        extend(row);
    } else {
        select(row);
    }
}

void TrackSelection::ensureSelected(int row) {
    if (!isSelected(row)) {
        select(row);
    }
}

int TrackSelection::rowTrackCount(int row) const {
    if (row < 0 || row >= rowCount()) {
        return 0;
    }
    if (const auto* albums = qobject_cast<const AlbumsModel*>(m_model.data())) {
        return static_cast<int>(albums->album(row).tracks.size());
    }
    return 1;
}

QList<int> TrackSelection::selectedRows() const {
    QList<int> rows;
    rows.reserve(m_count);
    const int limit = std::min(static_cast<int>(m_selected.size()), rowCount());
    for (int row = 0; row < limit; ++row) {
        if (m_selected[row] != 0) {
            rows.append(row);
        }
    }
    return rows;
}

QList<TrackInfo> TrackSelection::selectedTracks() const {
    QList<TrackInfo> tracks;
    if (const auto* albums = qobject_cast<const AlbumsModel*>(m_model.data())) {
        for (const int row : selectedRows()) {
            tracks.append(albums->album(row).tracks);
        }
    } else if (const auto* results = qobject_cast<const SearchResultsModel*>(m_model.data())) {
        tracks.reserve(m_count);
        for (const int row : selectedRows()) {
            tracks.append(results->track(row));
        }
    }
    return tracks;
}

QVariantList TrackSelection::tracks() const {
    QVariantList list;
    const QList<TrackInfo> selected = selectedTracks();
    list.reserve(selected.size());
    for (const TrackInfo& track : selected) {
        list.append(trackRef(track));
    }
    return list;
}

QString TrackSelection::query() const {
    if (m_trackCount <= TrackDragPayload::kMaxTracks) {
        return {};
    }
    if (const auto* albums = qobject_cast<const AlbumsModel*>(m_model.data())) {
        DragQuery query;
        for (const int row : selectedRows()) {
            query.albumIds.append(albums->album(row).info.id);
        }
        return toDragQuery(query);
    }
    // 還沒載完時選滿已載入的列不等於全選（allQuery 會帶到還沒載入、沒被選的曲目）
    const bool wholeList = m_all || (m_complete && m_count == rowCount());
    return wholeList ? m_allQuery : QString();
}

QString TrackSelection::payloadJson() const {
    TrackDragPayload payload;
    payload.query = query();
    if (!queryOnly()) {
        const QList<TrackInfo> selected = selectedTracks();
        payload.tracks.reserve(selected.size());
        for (const TrackInfo& track : selected) {
            payload.tracks.append(track.ref);
        }
        g_hint = selected.size() == 1 ? selected.first() : TrackInfo{};
    } else {
        g_hint = TrackInfo{};
    }
    return QString::fromUtf8(payload.toJson());
}

QString TrackSelection::searchQuery(const QString& text, const QString& artist,
                                    const QString& album, int year, const QString& format,
                                    int sampleRate) const {
    DragQuery query;
    query.criteria = {text, artist, album, year, format, sampleRate};
    return toDragQuery(query);
}

QString TrackSelection::folderQuery(qint64 folderId) const {
    if (folderId == 0) {
        return {};
    }
    DragQuery query;
    query.folderId = folderId;
    return toDragQuery(query);
}

QString TrackSelection::albumQuery(qint64 albumId) const {
    if (albumId == 0) {
        return {};
    }
    DragQuery query;
    query.albumIds = {albumId};
    return toDragQuery(query);
}

const TrackInfo& TrackSelection::trackHint() {
    return g_hint;
}

} // namespace auralis::mod::lib
