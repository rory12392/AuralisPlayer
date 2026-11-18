#pragma once

#include "playlist_parse.h"
#include "playlist_store.h"

#include <auralis/library_query.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <vector>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class PlaybackService;
} // namespace auralis

namespace auralis::mod::pl {

class PlaylistsController;

/// 播放清單頁一列要顯示的曲目資訊（向音樂庫查到的）。音樂庫裡沒有這首時 missing，
/// 曲名用檔名（之後匯入時的 #EXTINF 標題是另一張卡）。
struct TrackDisplay {
    bool missing = true;
    /// 音樂庫裡沒有、但檔案在磁碟上（S-08「不在音樂庫」，仍可播放；T-52）
    bool onDisk = false;
    QString title;
    QString artist;
    QString album;
    QString format;
    QString quality;
    QString coverKey;
    qint64 durationMs = -1;
};

/// 播放清單頁（S-08，US-11）的曲目清單，QML 以單例 PlaylistTracks 取用；頁面設 playlistId。
/// - 載入：項目讀 PlaylistStore，顯示資訊逐首 LibraryQuery::findByIdentity；兩者都在自己的背景
///   執行緒做（4 萬首也不卡介面），查過的身分留在快取，加入或復原後重讀只查新的。音樂庫掃描完
///   清掉快取重讀（找不到檔案的標記可能變了）。status：loading、ready、error。
/// - 選取（介面規範 §7 清單的鍵盤操作）：select、toggle（Ctrl）、extend（Shift）、
///   selectAll、click。
///   右鍵選單與拖放用 payloadJson()／selectedTracks()，內容是選取的曲目（照清單順序）。
/// - 播放：playFrom(row) 把整個播放清單放進佇列、從第 row 首開始。
/// - 排序：moveRows（拖曳把手放開時）、moveSelectionBy（鍵盤）先一次寫入資料庫（交易，中途關閉
///   不會半套），成功才在畫面上換順序（layoutChanged，列的位置與捲動位置不變）。
/// - removeSelected：移除選取的列；通知與「復原」在 PlaylistsController。
/// - 同一首出現兩次就是兩列，各自選取、移除、排序（位置就是列號）。
/// 執行緒：只在 UI 執行緒使用（背景工作的結果排回 UI 執行緒）。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PlaylistTracks : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlaylistTracks)
    QML_SINGLETON
    Q_PROPERTY(qint64 playlistId READ playlistId WRITE setPlaylistId NOTIFY playlistIdChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int count READ count NOTIFY summaryChanged)
    /// 找不到檔案的列數（S-08 頂部「<m> 首找不到檔案」，T-52）。
    Q_PROPERTY(int missingCount READ missingCount NOTIFY summaryChanged)
    /// 「<總長>」：找得到的曲目長度總和，m:ss 或 h:mm:ss。
    Q_PROPERTY(QString totalDurationText READ totalDurationText NOTIFY summaryChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    /// 選取每變一次加一：清單列的 highlighted 綁定它才會重算。
    Q_PROPERTY(int revision READ revision NOTIFY selectionChanged)
    /// 正在聽的曲目的鍵（keyOf），列比對它畫正在播放的標記；沒有時空字串。
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentKeyChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationTextRole,
        FormatRole,
        QualityTextRole,
        CoverUrlRole,
        MissingRole,
        PathRole,
        SubsongRole,
        KeyRole,
        /// 路徑在磁碟上、但不在音樂庫（S-08「不在音樂庫」，仍可播放；T-52）
        NotInLibraryRole,
    };

    /// library、playback 沒給時（測試）不查顯示資訊（全部 missing）、不能播放。
    PlaylistTracks(PlaylistStore& store, PlaylistsController& playlists, LibraryQuery* library,
                   PlaybackService* playback, QObject* parent = nullptr);
    ~PlaylistTracks() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 playlistId() const { return m_playlistId; }
    void setPlaylistId(qint64 id);
    QString status() const { return m_status; }
    int count() const { return static_cast<int>(m_rows.size()); }
    int missingCount() const;
    /// 音樂庫沒有的曲目查磁碟用的檔案查詢（預設 contentPathExists；T-52）。測試換掉它。
    void setExistsCheck(ExistsCheck exists);
    QString totalDurationText() const;
    int selectedCount() const { return m_selectedCount; }
    int revision() const { return m_revision; }
    QString currentKey() const { return m_currentKey; }

    /// 重讀（錯誤狀態的「重試」也用它）。
    Q_INVOKABLE void reload();

    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void extend(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// 滑鼠點擊：依目前按住的鍵（Ctrl → toggle、Shift → extend、都沒有 → select）。
    Q_INVOKABLE void click(int row);
    /// 右鍵與拖曳：這列沒被選取時改成只選它。
    Q_INVOKABLE void ensureSelected(int row);
    /// 選取的列號，由小到大。
    Q_INVOKABLE QList<int> selectedRows() const;
    /// 選取的曲目 [{path, subsong}]（拖放來源用）。
    Q_INVOKABLE QVariantList selectedTracks() const;
    /// 選取的曲目的 TrackDragPayload JSON（右鍵選單用，和拖放同一份）。
    Q_INVOKABLE QString payloadJson() const;

    /// 整個播放清單放進佇列，從第 row 首開始播。
    Q_INVOKABLE bool playFrom(int row);
    /// 標題列「播放」：從第一首找得到的開始播整個清單；全部找不到時不播、通知
    /// 「這個播放清單的曲目都找不到」，回傳 false（M-04 審查）。
    Q_INVOKABLE bool playAll();
    /// 從第 row 列開始拖曳時實際拖出去的首數（拖曳小卡的數字）：row 有選取時是選取的列裡
    /// 找得到的首數，沒選取時是 row 本身（找不到時 0）（M-04 審查）。
    Q_INVOKABLE int dragCount(int row) const;
    /// 把 rows 這幾列（一起）移到 target 之前（target 是移動前的列號，等於列數＝最後）。
    /// 回傳移動後第一列的位置；沒有變動或失敗時 -1。
    Q_INVOKABLE int moveRows(const QList<int>& rows, int target);
    /// 選取的每一列往上（-1）或往下（1）移一格，被擋住的（已經在最上或最下）不動。回傳是否有變動。
    Q_INVOKABLE bool moveSelectionBy(int delta);
    /// 移除選取的列；回傳是否移除。
    Q_INVOKABLE bool removeSelected();
    /// 把拖進來的曲目插在第 row 列之前（row 小於 0＝最後）。
    Q_INVOKABLE bool insertTracks(const QString& payloadJson, int row);

    /// 曲目的鍵：pathKey 與子曲目編號。
    Q_INVOKABLE static QString keyOf(const QString& path, int subsong);

    /// 模組 start() 時設定；QML 引擎取單例時用它。
    static void setInstance(PlaylistTracks* instance);
    static PlaylistTracks* create(QQmlEngine*, QJSEngine*);

signals:
    void playlistIdChanged();
    void statusChanged();
    void summaryChanged();
    void selectionChanged();
    void currentKeyChanged();

private:
    struct Row {
        PlaylistItem item;
        TrackDisplay display;
    };
    struct Loaded;

    void applyLoaded(quint64 generation, const std::shared_ptr<Loaded>& loaded);
    /// 先寫入資料庫（重排），成功後在畫面上換順序並保留選取。
    bool applyOrder(const QList<int>& order);
    void onItemsChanged(qint64 id);
    void setStatus(const QString& status);
    void selectionDone();
    void updateCurrent();

    PlaylistStore& m_store;
    PlaylistsController& m_playlists;
    LibraryQuery* m_library = nullptr;
    PlaybackService* m_playback = nullptr;

    qint64 m_playlistId = 0;
    QString m_status = QStringLiteral("loading");
    QList<Row> m_rows;
    std::vector<char> m_selected;
    int m_selectedCount = 0;
    int m_revision = 0;
    int m_anchor = -1;
    QString m_currentKey;
    /// 已查過的顯示資訊（keyOf → TrackDisplay）
    QHash<QString, TrackDisplay> m_cache;
    ExistsCheck m_exists;
    /// 自己寫入時不重讀（畫面已經換好了）
    bool m_selfChange = false;
    /// 最新一次載入的編號；背景工作看到不是自己時提早結束
    std::shared_ptr<std::atomic<quint64>> m_generation = std::make_shared<std::atomic<quint64>>(0);
    QThreadPool m_pool;
};

} // namespace auralis::mod::pl
