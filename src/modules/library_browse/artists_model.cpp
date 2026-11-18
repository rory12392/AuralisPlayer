#include "artists_model.h"

namespace auralis::mod::lib {

int ArtistsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_artists.size()) + 1;
}

QVariant ArtistsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() > m_artists.size()) {
        return {};
    }
    const bool all = index.row() == 0;
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return all ? QString() : artist(index.row()).name;
    case AlbumCountRole:
        return all ? m_totalAlbums : artist(index.row()).albumCount;
    case AllRole:
        return all;
    default:
        return {};
    }
}

QHash<int, QByteArray> ArtistsModel::roleNames() const {
    return {{NameRole, "name"}, {AlbumCountRole, "albumCount"}, {AllRole, "all"}};
}

int ArtistsModel::rowOf(const QString& name) const {
    for (qsizetype i = 0; i < m_artists.size(); ++i) {
        if (m_artists.at(i).name == name) {
            return static_cast<int>(i) + 1;
        }
    }
    return -1;
}

void ArtistsModel::update(QList<ArtistEntry> artists, int totalAlbums) {
    if (totalAlbums != m_totalAlbums) {
        m_totalAlbums = totalAlbums;
        emit dataChanged(index(0), index(0), {AlbumCountRole});
    }
    // 兩份清單排序相同：依序比對，舊的多出來的移除、新的多出來的插入（列號含「全部藝人」那一列）
    qsizetype i = 0; // m_artists 的位置（同時是目前畫面上的位置）
    qsizetype j = 0; // artists 的位置
    while (i < m_artists.size() || j < artists.size()) {
        const int row = static_cast<int>(i) + 1;
        if (j >= artists.size() ||
            (i < m_artists.size() && artistLess(m_artists.at(i).name, artists.at(j).name))) {
            beginRemoveRows({}, row, row);
            m_artists.removeAt(i);
            endRemoveRows();
        } else if (i >= m_artists.size() || artistLess(artists.at(j).name, m_artists.at(i).name)) {
            beginInsertRows({}, row, row);
            m_artists.insert(i, artists.at(j));
            endInsertRows();
            ++i;
            ++j;
        } else {
            if (m_artists.at(i).albumCount != artists.at(j).albumCount) {
                m_artists[i].albumCount = artists.at(j).albumCount;
                emit dataChanged(index(row), index(row), {AlbumCountRole});
            }
            ++i;
            ++j;
        }
    }
}

} // namespace auralis::mod::lib
