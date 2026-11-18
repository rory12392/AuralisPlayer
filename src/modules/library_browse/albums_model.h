#pragma once

#include "album_list.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace auralis::mod::lib {

/// 專輯頁（S-03）三種檢視共用的清單模型，由 AlbumsController 填入（已篩選、已排序）。
/// 角色：albumId、title、artist、year、trackCount、durationText、formatText、coverUrl
/// （沒有封面時空字串，Cover 畫預設圖）、tracks（詳細檢視的曲目列：
/// [{ trackNo, discNo, title, durationText, qualityText, path, subsong }]，用到時才組）。
/// 執行緒：只在 UI 執行緒使用。
class AlbumsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 LibraryAlbums.albums 提供")

public:
    enum Role {
        AlbumIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        YearRole,
        TrackCountRole,
        DurationTextRole,
        FormatTextRole,
        CoverUrlRole,
        TracksRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 第 row 張專輯（row 要在範圍內）。
    const AlbumEntry& album(int row) const { return m_albums.at(row); }

    /// 換成新的清單。和目前清單相比只是新增或移除（例：掃描中新專輯逐步出現）時，
    /// 用插入與移除通知，留下來的列不重建，畫面不閃、捲動位置不變；
    /// 留下來的專輯順序變了（換排序）才整個重設。內容有變的列發 dataChanged。
    void update(QList<AlbumEntry> albums);

private:
    QList<AlbumEntry> m_albums;
};

} // namespace auralis::mod::lib
