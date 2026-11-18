#pragma once

#include "artist_list.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace auralis::mod::lib {

/// 藝人頁（S-05）左欄的清單模型：第 0 列是「全部藝人」（all 為 true，albumCount 是專輯總數），
/// 之後是各藝人（groupArtists 的順序）。角色：name、albumCount、all。
/// 執行緒：只在 UI 執行緒使用。
class ArtistsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 LibraryArtists.artists 提供")

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        AlbumCountRole,
        AllRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 藝人的列（「全部藝人」之後）；沒有這位時 -1。
    int rowOf(const QString& name) const;
    /// 第 row 列的藝人（row ≥ 1 且在範圍內）。
    const ArtistEntry& artist(int row) const { return m_artists.at(row - 1); }

    /// 換成新的清單（兩邊都依 groupArtists 的順序）。只用插入與移除通知，留下來的列不重建，
    /// 掃描中新藝人出現時捲動位置不變；專輯數變了的列發 dataChanged。
    void update(QList<ArtistEntry> artists, int totalAlbums);

private:
    QList<ArtistEntry> m_artists;
    int m_totalAlbums = 0;
};

} // namespace auralis::mod::lib
