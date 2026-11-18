#pragma once

#include "albums_model.h"
#include "artists_model.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis::mod::lib {

class AlbumsController;

/// 藝人頁（S-05，US-10 AC-3）的資料，QML 以單例 LibraryArtists 取用。
/// - 不另外讀音樂庫：用 LibraryAlbums 讀好的整個音樂庫的專輯（allAlbums），以顯示藝人分組
///   （groupArtists：專輯藝人，沒有時第一首的藝人；「Various Artists」不拆開）。
/// - artists：左欄（第 0 列「全部藝人」）；albums：所選藝人的專輯，排序跟專輯頁（LIB/albumsSort），
///   檢視方式由頁面用 LibraryAlbums.view；不套篩選列（S-05 沒有篩選列）。
/// - 選取以名稱記住：重讀後那位藝人還在就留著，不在了回到「全部藝人」。
/// 三種專輯檢視共用的介面（albums、playAlbum、albumIdAt、currentAlbumId）和 LibraryAlbums 相同。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class ArtistsController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryArtists)
    QML_SINGLETON
    Q_PROPERTY(auralis::mod::lib::ArtistsModel* artists READ artists CONSTANT)
    Q_PROPERTY(auralis::mod::lib::AlbumsModel* albums READ albums CONSTANT)
    /// 左欄選取的列（0＝全部藝人）。
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    /// 所選藝人的名稱；「全部藝人」時空字串（看 selectedRow）。
    Q_PROPERTY(QString selectedArtist READ selectedArtist NOTIFY selectionChanged)
    /// 右邊的專輯數。
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// 藝人數（不含「全部藝人」）。
    Q_PROPERTY(int artistCount READ artistCount NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(qint64 currentAlbumId READ currentAlbumId NOTIFY currentAlbumChanged)

public:
    explicit ArtistsController(AlbumsController& albums, QObject* parent = nullptr);

    ArtistsModel* artists() { return &m_artists; }
    AlbumsModel* albums() { return &m_albums; }
    int selectedRow() const;
    QString selectedArtist() const { return m_all ? QString() : m_selected; }
    int count() const { return m_albums.rowCount(); }
    int artistCount() const { return m_artists.rowCount() - 1; }
    bool loaded() const;
    bool busy() const;
    QString error() const;
    qint64 currentAlbumId() const;

    /// 第一次顯示：專輯還沒讀過才讀（同 LibraryAlbums.load）。
    Q_INVOKABLE void load();
    /// 重讀（掃描中定時、錯誤的「重試」）。
    Q_INVOKABLE void reload();
    /// 選左欄第 row 列；不在範圍時不做事。
    Q_INVOKABLE void select(int row);
    /// 依名稱選藝人（開頁參數 {artist}，T-25）：先找同名分組；找不到時找第一張「有一首的曲目藝人
    /// 等於這個名稱」的專輯，選它所屬的分組（客串、Various Artists
    /// 落在專輯藝人）；都找不到或空字串時 選「全部藝人」。還沒讀完時先記住名稱，讀完再對。
    Q_INVOKABLE void selectArtist(const QString& name);
    /// 播放右邊第 row 張專輯，從第 from 首開始（同 LibraryAlbums.playAlbum）。
    Q_INVOKABLE bool playAlbum(int row, int from = 0);
    /// 右邊第 row 張專輯的 id；不在範圍時 0。
    Q_INVOKABLE qint64 albumIdAt(int row) const;

    static void setInstance(ArtistsController* instance);
    static ArtistsController* create(QQmlEngine*, QJSEngine*);

signals:
    void selectionChanged();
    void changed();
    void currentAlbumChanged();

private:
    /// 依 allAlbums 重組左欄，再更新右邊。
    void rebuild();
    /// 依目前選取與排序更新右邊。
    void applySelection();
    /// 開頁參數的名稱 → 分組名稱（見 selectArtist）；找不到時空字串。
    QString resolveArtist(const QString& name) const;

    AlbumsController& m_source;
    ArtistsModel m_artists;
    AlbumsModel m_albums;
    bool m_all = true;
    QString m_selected;
    /// selectArtist 在讀完前收到的名稱還沒對過分組（m_selected 暫放原本的名稱）。
    bool m_resolvePending = false;
};

} // namespace auralis::mod::lib
