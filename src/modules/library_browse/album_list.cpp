#include "album_list.h"

#include <QHash>

#include <algorithm>

namespace auralis::mod::lib {

AlbumSort albumSortFromName(const QString& name) {
    if (name == QLatin1String("title")) {
        return AlbumSort::Title;
    }
    if (name == QLatin1String("artist")) {
        return AlbumSort::Artist;
    }
    if (name == QLatin1String("year")) {
        return AlbumSort::Year;
    }
    return AlbumSort::Added;
}

QString albumSortName(AlbumSort sort) {
    switch (sort) {
    case AlbumSort::Title:
        return QStringLiteral("title");
    case AlbumSort::Artist:
        return QStringLiteral("artist");
    case AlbumSort::Year:
        return QStringLiteral("year");
    case AlbumSort::Added:
        break;
    }
    return QStringLiteral("added");
}

QString albumFormatText(const QList<TrackInfo>& tracks) {
    const TrackInfo* best = nullptr;
    for (const TrackInfo& t : tracks) {
        if (best == nullptr || t.sampleRate > best->sampleRate ||
            (t.sampleRate == best->sampleRate && t.bits > best->bits)) {
            best = &t;
        }
    }
    if (best == nullptr) {
        return {};
    }
    if (best->dsdRate > 0 || best->codec == QLatin1String("DSD")) {
        return formatKey(*best);
    }
    if (best->bits <= 0 || best->sampleRate <= 0) {
        return best->codec; // 有損格式（或不知道取樣率）只寫格式名
    }
    // 44100 →「44.1」、96000 →「96」
    const QString rate =
        QString::number(best->sampleRate / 1000.0, 'f', 1).remove(QStringLiteral(".0"));
    return rate + QLatin1Char('/') + QString::number(best->bits);
}

QList<AlbumEntry> buildAlbums(const QList<AlbumInfo>& albums, const QList<TrackInfo>& tracks) {
    QList<AlbumEntry> entries;
    entries.reserve(albums.size());
    QHash<qint64, qsizetype> indexById;
    indexById.reserve(albums.size());
    for (const AlbumInfo& album : albums) {
        indexById.insert(album.id, entries.size());
        AlbumEntry entry;
        entry.info = album;
        entries.append(std::move(entry));
    }
    for (const TrackInfo& track : tracks) {
        const auto it = indexById.constFind(track.albumId);
        if (track.albumId != 0 && it != indexById.cend()) {
            AlbumEntry& entry = entries[*it];
            entry.durationMs += track.durationMs;
            entry.tracks.append(track);
        }
    }
    for (AlbumEntry& entry : entries) {
        entry.artist = entry.info.albumArtist;
        if (entry.artist.isEmpty() && !entry.tracks.isEmpty()) {
            entry.artist = entry.tracks.first().artist;
        }
        entry.formatText = albumFormatText(entry.tracks);
    }
    return entries;
}

bool albumMatches(const AlbumEntry& album, const SearchCriteria& criteria) {
    const bool noFilter = criteria.artist.isEmpty() && criteria.album.isEmpty() &&
                          criteria.year == 0 && criteria.format.isEmpty() &&
                          criteria.sampleRate == 0;
    if (noFilter) {
        return true;
    }
    return std::any_of(album.tracks.cbegin(), album.tracks.cend(), [&](const TrackInfo& t) {
        if (!criteria.artist.isEmpty() && t.artist != criteria.artist &&
            t.albumArtist != criteria.artist) {
            return false;
        }
        if (!criteria.album.isEmpty() && t.album != criteria.album) {
            return false;
        }
        if (criteria.year != 0 && t.year != criteria.year) {
            return false;
        }
        if (!criteria.format.isEmpty() && formatKey(t) != criteria.format) {
            return false;
        }
        return criteria.sampleRate == 0 || t.sampleRate == criteria.sampleRate;
    });
}

void sortAlbums(QList<AlbumEntry>& albums, AlbumSort sort) {
    const auto byTitle = [](const AlbumEntry& a, const AlbumEntry& b) {
        const int c = QString::compare(a.info.title, b.info.title, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.info.id < b.info.id;
    };
    std::stable_sort(albums.begin(), albums.end(), [&](const AlbumEntry& a, const AlbumEntry& b) {
        switch (sort) {
        case AlbumSort::Added:
            return a.info.id > b.info.id;
        case AlbumSort::Title:
            return byTitle(a, b);
        case AlbumSort::Artist: {
            const int c = QString::compare(a.artist, b.artist, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : byTitle(a, b);
        }
        case AlbumSort::Year:
            return a.info.year != b.info.year ? a.info.year > b.info.year : byTitle(a, b);
        }
        return false;
    });
}

} // namespace auralis::mod::lib
