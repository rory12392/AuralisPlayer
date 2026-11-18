#pragma once

#include <auralis/library_query.h>

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

namespace auralis::mod::lib {

/// 搜尋與篩選的結果（S-07 的曲目清單），由 SearchController 分批填入。
/// 角色：title、artist、albumArtist、album、composer、year、discNo、trackNo、durationMs、
/// format（formatKey）、sampleRate、bits、coverKey、path、subsong、albumId；
/// 顯示用的 durationText、qualityText、coverUrl。
/// 執行緒：只在 UI 執行緒使用。
class SearchResultsModel : public QAbstractListModel {
    Q_OBJECT
    // 有名稱的型別（不能在 QML 建立）：頁面把 LibrarySearch.results 當清單模型時 qmllint 才認得
    QML_ELEMENT
    QML_UNCREATABLE("由 LibrarySearch.results 提供")

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumArtistRole,
        AlbumRole,
        ComposerRole,
        YearRole,
        DiscNoRole,
        TrackNoRole,
        DurationMsRole,
        FormatRole,
        SampleRateRole,
        BitsRole,
        CoverKeyRole,
        PathRole,
        SubsongRole,
        AlbumIdRole,
        // 清單列直接顯示的文字（T-21）：長度、取樣率／位元、封面網址（沒有封面時空字串）
        DurationTextRole,
        QualityTextRole,
        CoverUrlRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 第 row 列的曲目（row 要在範圍內）。
    const TrackInfo& track(int row) const { return m_tracks.at(row); }

    /// 換成新查詢的第一批結果。
    void reset(QList<TrackInfo> tracks);
    /// 接上同一個查詢的下一批。
    void append(const QList<TrackInfo>& tracks);

private:
    QList<TrackInfo> m_tracks;
};

} // namespace auralis::mod::lib
