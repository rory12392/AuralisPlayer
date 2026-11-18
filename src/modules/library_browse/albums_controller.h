#pragma once

#include "album_list.h"
#include "albums_model.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class LibraryQuery;
class ModuleSettings;
class PlaybackService;
} // namespace auralis

namespace auralis::mod::lib {

class SearchController;

/// 專輯頁（S-03，US-10 AC-1、2、5）的資料，QML 以單例 LibraryAlbums 取用。
/// - 在自己的工作執行緒讀整個音樂庫的專輯與曲目（LibraryQuery::albums、tracks），組成 AlbumEntry；
///   掃描完成（scanFinished）時自動重讀，掃描中由頁面定時呼叫 reload()，新專輯逐步出現。
/// - 篩選跟著 LibrarySearch 的篩選條件（不含搜尋字串），排序是 sort；結果放在 albums。
/// - view（grid、list、detail）與 sort（added、title、artist、year）存在 LIB/albumsView、
///   LIB/albumsSort，重新啟動後保留（AC-10-5）。
/// 執行緒：只在 UI 執行緒使用；讀取結果以 queued 呼叫交回。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class AlbumsController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryAlbums)
    QML_SINGLETON
    Q_PROPERTY(auralis::mod::lib::AlbumsModel* albums READ albums CONSTANT)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    /// 篩選後的專輯數（「<n> 張專輯」）。
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// 音樂庫的專輯總數（不看篩選）。
    Q_PROPERTY(int total READ total NOTIFY changed)
    /// 讀過一次了（之前是載入中）。
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// 讀取失敗的原因；成功時空字串。
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// 正在聽的曲目所屬的專輯 id（封面牆的 1px ink 框）；不在音樂庫的專輯裡時是 0。
    Q_PROPERTY(qint64 currentAlbumId READ currentAlbumId NOTIFY currentAlbumChanged)

public:
    /// settings 為 nullptr 時不保存檢視與排序（測試用）。
    AlbumsController(LibraryQuery& library, PlaybackService& playback, SearchController& search,
                     ModuleSettings* settings, QObject* parent = nullptr);
    /// 等工作執行緒結束（同 shutdown()）。
    ~AlbumsController() override;

    AlbumsModel* albums() { return &m_model; }
    QString view() const { return m_view; }
    QString sort() const { return albumSortName(m_sort); }
    void setView(const QString& view);
    void setSort(const QString& sort);
    int count() const { return m_model.rowCount(); }
    int total() const { return static_cast<int>(m_all.size()); }
    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    qint64 currentAlbumId() const { return m_currentAlbumId; }

    /// 第一次顯示：還沒讀過才讀。
    Q_INVOKABLE void load();
    /// 重讀整個音樂庫（掃描中定時呼叫、錯誤時的「重試」）；正在讀時讀完再讀一次。
    Q_INVOKABLE void reload();
    /// 播放第 row 張專輯：佇列換成整張專輯，從第 from 首開始（封面的播放鍵是 0；詳細檢視雙擊曲目
    /// 是那一首）。row 或 from 不在範圍時不做事、回傳 false。
    Q_INVOKABLE bool playAlbum(int row, int from = 0);
    /// 第 row 張專輯的 id（開專輯詳細頁的參數）；不在範圍時 0。
    Q_INVOKABLE qint64 albumIdAt(int row) const;
    /// 右鍵選單「前往專輯」「前往藝人」（T-25）：payloadJson 只有一首時回傳 {albumId, artist}
    /// （artist 是藝人頁分組用的專輯顯示藝人）；不是一首、或找不到所屬專輯時空物件。
    /// 先找讀好的專輯，找不到（例：還沒讀過）時用清單開選單時記下的那首（TrackSelection::trackHint）。
    Q_INVOKABLE QVariantMap trackLocation(const QString& payloadJson) const;
    /// 佇列換成 album 的曲目，從第 from 首開始播（藝人頁 T-23 也用）；from 不在範圍時回傳 false。
    bool playEntry(const AlbumEntry& album, int from);

    /// 整個音樂庫的專輯（不看篩選、沒排序），給藝人頁（T-23）分組。
    const QList<AlbumEntry>& allAlbums() const { return m_all; }
    AlbumSort sortValue() const { return m_sort; }

    /// 模組停止時：丟掉排隊中的讀取、等正在讀的結束。之後不再更新。
    void shutdown();

    static void setInstance(AlbumsController* instance);
    static AlbumsController* create(QQmlEngine*, QJSEngine*);

signals:
    void viewChanged();
    void sortChanged();
    void changed();
    void currentAlbumChanged();
    /// 讀完一次音樂庫（allAlbums 換了）。
    void libraryChanged();

private:
    void onLoaded(quint64 generation, QList<AlbumEntry> albums, const QString& error);
    /// 依目前的篩選與排序重排 m_all 交給模型。
    void apply();
    void onCriteriaChanged();
    void updateCurrentAlbum();

    LibraryQuery& m_library;
    PlaybackService& m_playback;
    SearchController& m_search;
    ModuleSettings* m_settings;
    AlbumsModel m_model;
    QList<AlbumEntry> m_all;
    SearchCriteria m_filter; ///< 上次套用的篩選（text 一律空白）
    QString m_view;
    AlbumSort m_sort = AlbumSort::Added;
    bool m_loaded = false;
    bool m_busy = false;
    bool m_reloadPending = false;
    QString m_error;
    qint64 m_currentAlbumId = 0;
    std::shared_ptr<std::atomic<quint64>> m_generation = std::make_shared<std::atomic<quint64>>(0);
    // 最後宣告、最先解構：解構時先等讀取結束
    QThreadPool m_pool;
};

} // namespace auralis::mod::lib
