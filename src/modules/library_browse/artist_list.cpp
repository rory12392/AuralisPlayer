#include "artist_list.h"

#include <QHash>

#include <algorithm>

namespace auralis::mod::lib {

QList<ArtistEntry> groupArtists(const QList<AlbumEntry>& albums) {
    QHash<QString, int> counts;
    for (const AlbumEntry& album : albums) {
        ++counts[album.artist];
    }
    QList<ArtistEntry> artists;
    artists.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        artists.append({it.key(), it.value()});
    }
    std::sort(artists.begin(), artists.end(), [](const ArtistEntry& a, const ArtistEntry& b) {
        return artistLess(a.name, b.name);
    });
    return artists;
}

bool artistLess(const QString& a, const QString& b) {
    if (a.isEmpty() != b.isEmpty()) {
        return b.isEmpty(); // 未知藝人排最後
    }
    const int c = QString::compare(a, b, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : a < b;
}

QList<AlbumEntry> albumsOfArtist(const QList<AlbumEntry>& albums, const QString& name) {
    QList<AlbumEntry> result;
    for (const AlbumEntry& album : albums) {
        if (album.artist == name) {
            result.append(album);
        }
    }
    return result;
}

} // namespace auralis::mod::lib
