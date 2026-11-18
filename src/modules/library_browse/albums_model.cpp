#include "albums_model.h"

#include "track_text.h"

#include <auralis/cover_service.h>

#include <QSet>
#include <QVariantMap>

namespace auralis::mod::lib {

namespace {

/// 畫面上看得到的欄位有沒有變（曲目只比數量與總長，夠判斷要不要重畫）。
bool sameContent(const AlbumEntry& a, const AlbumEntry& b) {
    return a.info.title == b.info.title && a.info.year == b.info.year &&
           a.info.coverKey == b.info.coverKey && a.info.trackCount == b.info.trackCount &&
           a.artist == b.artist && a.durationMs == b.durationMs && a.formatText == b.formatText &&
           a.tracks.size() == b.tracks.size();
}

QVariantList trackRows(const AlbumEntry& album) {
    QVariantList rows;
    rows.reserve(album.tracks.size());
    for (const TrackInfo& t : album.tracks) {
        rows.append(QVariantMap{{QStringLiteral("trackNo"), t.trackNo},
                                {QStringLiteral("discNo"), t.discNo},
                                {QStringLiteral("title"), t.title},
                                {QStringLiteral("durationText"), durationText(t.durationMs)},
                                {QStringLiteral("qualityText"), qualityText(t)},
                                {QStringLiteral("path"), t.ref.path},
                                {QStringLiteral("subsong"), t.ref.subsong}});
    }
    return rows;
}

} // namespace

int AlbumsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_albums.size());
}

QVariant AlbumsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_albums.size()) {
        return {};
    }
    const AlbumEntry& a = m_albums.at(index.row());
    switch (role) {
    case AlbumIdRole:
        return a.info.id;
    case Qt::DisplayRole:
    case TitleRole:
        return a.info.title;
    case ArtistRole:
        return a.artist;
    case YearRole:
        return a.info.year;
    case TrackCountRole:
        return static_cast<int>(a.tracks.isEmpty() ? a.info.trackCount : a.tracks.size());
    case DurationTextRole:
        return durationText(a.durationMs);
    case FormatTextRole:
        return a.formatText;
    case CoverUrlRole:
        return a.info.coverKey.isEmpty() ? QString() : CoverService::imageUrl(a.info.coverKey);
    case TracksRole:
        return trackRows(a);
    default:
        return {};
    }
}

QHash<int, QByteArray> AlbumsModel::roleNames() const {
    return {{AlbumIdRole, "albumId"},       {TitleRole, "title"},
            {ArtistRole, "artist"},         {YearRole, "year"},
            {TrackCountRole, "trackCount"}, {DurationTextRole, "durationText"},
            {FormatTextRole, "formatText"}, {CoverUrlRole, "coverUrl"},
            {TracksRole, "tracks"}};
}

void AlbumsModel::update(QList<AlbumEntry> albums) {
    QSet<qint64> newIds;
    newIds.reserve(albums.size());
    for (const AlbumEntry& a : albums) {
        newIds.insert(a.info.id);
    }
    QSet<qint64> oldIds;
    oldIds.reserve(m_albums.size());
    for (const AlbumEntry& a : m_albums) {
        oldIds.insert(a.info.id);
    }
    // 留下來的專輯在新舊清單裡的先後要一樣，才能只用插入與移除
    QList<qint64> keptOld;
    for (const AlbumEntry& a : m_albums) {
        if (newIds.contains(a.info.id)) {
            keptOld.append(a.info.id);
        }
    }
    QList<qint64> keptNew;
    for (const AlbumEntry& a : albums) {
        if (oldIds.contains(a.info.id)) {
            keptNew.append(a.info.id);
        }
    }
    if (keptOld != keptNew) {
        beginResetModel();
        m_albums = std::move(albums);
        endResetModel();
        return;
    }
    // 1. 由後往前移除不在新清單的列（連續的一次移除）
    for (qsizetype i = m_albums.size() - 1; i >= 0;) {
        if (newIds.contains(m_albums.at(i).info.id)) {
            --i;
            continue;
        }
        qsizetype first = i;
        while (first > 0 && !newIds.contains(m_albums.at(first - 1).info.id)) {
            --first;
        }
        beginRemoveRows({}, static_cast<int>(first), static_cast<int>(i));
        m_albums.remove(first, i - first + 1);
        endRemoveRows();
        i = first - 1;
    }
    // 2. 照新清單的位置插入新的列（連續的一次插入）
    for (qsizetype i = 0; i < albums.size();) {
        if (i < m_albums.size() && m_albums.at(i).info.id == albums.at(i).info.id) {
            ++i;
            continue;
        }
        qsizetype last = i;
        while (last + 1 < albums.size() && !oldIds.contains(albums.at(last + 1).info.id)) {
            ++last;
        }
        beginInsertRows({}, static_cast<int>(i), static_cast<int>(last));
        for (qsizetype k = i; k <= last; ++k) {
            m_albums.insert(k, albums.at(k));
        }
        endInsertRows();
        i = last + 1;
    }
    // 3. 留下來的列換成新內容，有變的才通知
    for (qsizetype i = 0; i < albums.size(); ++i) {
        const bool changed = !sameContent(m_albums.at(i), albums.at(i));
        m_albums[i] = std::move(albums[i]);
        if (changed) {
            const QModelIndex at = index(static_cast<int>(i));
            emit dataChanged(at, at);
        }
    }
}

} // namespace auralis::mod::lib
