#pragma once

#include "album_list.h"
#include "search_results_model.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class PlaybackService;
} // namespace auralis

namespace auralis::mod::lib {

class AlbumsController;

/// 專輯詳細頁（S-04，US-10 AC-4）的資料與播放動作，QML 以單例 LibraryAlbum 取用。
/// - 不另外讀音樂庫：從 LibraryAlbums 讀好的整個音樂庫（allAlbums）以 albumId 找專輯；
///   LibraryAlbums 重讀（掃描完成、重試）後重新找；專輯被移除時 found 是 false，
///   頁面顯示錯誤區塊。
/// - tracks：專輯的曲目（依碟號、軌號），角色同 LibrarySearch.results；多片時頁面依 discNo 分組。
/// - play(from)：佇列換成整張專輯、從第 from 首播（AC-10-4，同 LibraryAlbums.playAlbum）；
///   enqueueAll()：整張專輯加到佇列最後（PlaybackService::enqueueLast，同 Player.enqueueLast）。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class AlbumDetailController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryAlbum)
    QML_SINGLETON
    Q_PROPERTY(qint64 albumId READ albumId NOTIFY changed)
    /// 專輯讀過一次了（之前是載入中）；轉自 LibraryAlbums。
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    /// 讀取失敗的原因（轉自 LibraryAlbums）；成功時空字串。
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// 讀過了而且音樂庫裡有這張專輯。
    Q_PROPERTY(bool found READ found NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    /// 顯示用的專輯藝人（沒有專輯藝人時第一首的藝人，同專輯頁）。
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    /// 等寬的「<年份>　<n> 首　<總長>　<格式摘要>」（沒有年份或格式時省略那一段）。
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)
    /// 不同碟號的數目；大於 1 時曲目清單依碟分組（「碟 <n>」）。
    Q_PROPERTY(int discCount READ discCount NOTIFY changed)
    /// 有曲目的藝人和專輯藝人不同（要顯示「藝人」欄）。
    Q_PROPERTY(bool hasTrackArtists READ hasTrackArtists NOTIFY changed)
    Q_PROPERTY(auralis::mod::lib::SearchResultsModel* tracks READ tracks CONSTANT)

public:
    AlbumDetailController(AlbumsController& albums, PlaybackService& playback,
                          QObject* parent = nullptr);

    qint64 albumId() const { return m_albumId; }
    bool loaded() const;
    QString error() const;
    bool found() const { return m_found; }
    QString title() const { return m_entry.info.title; }
    QString artist() const { return m_entry.artist; }
    QString summaryText() const;
    QString coverUrl() const;
    int trackCount() const { return static_cast<int>(m_entry.tracks.size()); }
    int discCount() const { return m_discCount; }
    bool hasTrackArtists() const { return m_hasTrackArtists; }
    SearchResultsModel* tracks() { return &m_tracks; }

    /// 顯示 albumId 這張專輯（開頁參數）；專輯還沒讀過時開始讀（同 LibraryAlbums.load）。
    Q_INVOKABLE void open(qint64 albumId);
    /// 重讀整個音樂庫（錯誤區塊的「重試」）。
    Q_INVOKABLE void reload();
    /// 佇列換成整張專輯、從第 from 首開始播（「播放專輯」是 0，雙擊或 Enter 是那一首）；
    /// 找不到專輯或 from 不在範圍時不做事、回傳 false。
    Q_INVOKABLE bool play(int from);
    /// 整張專輯加到佇列最後；找不到專輯或沒有曲目時回傳 false。
    Q_INVOKABLE bool enqueueAll();

    static void setInstance(AlbumDetailController* instance);
    static AlbumDetailController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    /// 從 LibraryAlbums 的專輯重新找 m_albumId。
    void refresh();

    AlbumsController& m_source;
    PlaybackService& m_playback;
    qint64 m_albumId = 0;
    bool m_found = false;
    AlbumEntry m_entry;
    int m_discCount = 0;
    bool m_hasTrackArtists = false;
    SearchResultsModel m_tracks;
};

} // namespace auralis::mod::lib
