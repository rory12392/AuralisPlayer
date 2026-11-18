#include "search_results_model.h"

#include "search_criteria.h"
#include "track_text.h"

#include <auralis/cover_service.h>

namespace auralis::mod::lib {

int SearchResultsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tracks.size());
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return {};
    }
    const TrackInfo& t = m_tracks.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return t.title;
    case ArtistRole:
        return t.artist;
    case AlbumArtistRole:
        return t.albumArtist;
    case AlbumRole:
        return t.album;
    case ComposerRole:
        return t.composer;
    case YearRole:
        return t.year;
    case DiscNoRole:
        return t.discNo;
    case TrackNoRole:
        return t.trackNo;
    case DurationMsRole:
        return t.durationMs;
    case FormatRole:
        return formatKey(t);
    case SampleRateRole:
        return t.sampleRate;
    case BitsRole:
        return t.bits;
    case CoverKeyRole:
        return t.coverKey;
    case PathRole:
        return t.ref.path;
    case SubsongRole:
        return t.ref.subsong;
    case AlbumIdRole:
        return t.albumId;
    case DurationTextRole:
        return durationText(t.durationMs);
    case QualityTextRole:
        return qualityText(t);
    case CoverUrlRole:
        return t.coverKey.isEmpty() ? QString() : CoverService::imageUrl(t.coverKey);
    default:
        return {};
    }
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const {
    return {{TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumArtistRole, "albumArtist"},
            {AlbumRole, "album"},
            {ComposerRole, "composer"},
            {YearRole, "year"},
            {DiscNoRole, "discNo"},
            {TrackNoRole, "trackNo"},
            {DurationMsRole, "durationMs"},
            {FormatRole, "format"},
            {SampleRateRole, "sampleRate"},
            {BitsRole, "bits"},
            {CoverKeyRole, "coverKey"},
            {PathRole, "path"},
            {SubsongRole, "subsong"},
            {AlbumIdRole, "albumId"},
            {DurationTextRole, "durationText"},
            {QualityTextRole, "qualityText"},
            {CoverUrlRole, "coverUrl"}};
}

void SearchResultsModel::reset(QList<TrackInfo> tracks) {
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

void SearchResultsModel::append(const QList<TrackInfo>& tracks) {
    if (tracks.isEmpty()) {
        return;
    }
    const int first = static_cast<int>(m_tracks.size());
    beginInsertRows({}, first, first + static_cast<int>(tracks.size()) - 1);
    m_tracks.append(tracks);
    endInsertRows();
}

} // namespace auralis::mod::lib
