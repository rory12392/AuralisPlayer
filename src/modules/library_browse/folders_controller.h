#pragma once

#include "folders_model.h"
#include "search_results_model.h"

#include <QObject>
#include <QThreadPool>
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

/// 資料夾頁（S-06，US-10 AC-3）的資料，QML 以單例 LibraryFolders 取用。
/// - tree：使用者加入的音樂資料夾與子資料夾（folders 表，結構與磁碟一致），在自己的工作執行緒讀，
///   同時檢查每個資料夾在磁碟上還在不在（missing）；掃描完成自動重讀。
/// - tracks：所選資料夾（含子資料夾）的曲目，列的角色同 LibrarySearch.results；換選取時在同一條
///   工作執行緒讀（新選取讓還沒開始的讀取作廢）。
/// - paneWidth：左欄寬度（預設 320，拖曳調整），存在 LIB/foldersPaneWidth。
/// 執行緒：只在 UI 執行緒使用；讀取結果以 queued 呼叫交回。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class FoldersController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryFolders)
    QML_SINGLETON
    Q_PROPERTY(auralis::mod::lib::FoldersModel* tree READ tree CONSTANT)
    Q_PROPERTY(auralis::mod::lib::SearchResultsModel* tracks READ tracks CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// 左欄選取的列；沒有選取時 -1。
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedMissing READ selectedMissing NOTIFY selectionChanged)
    /// 所選資料夾的 id（全選拖放時的查詢條件，T-25）；沒有選取時 0。
    Q_PROPERTY(qint64 selectedFolderId READ selectedFolderId NOTIFY selectionChanged)
    /// 所選資料夾的曲目讀過了（之前是載入中）。
    Q_PROPERTY(bool tracksLoaded READ tracksLoaded NOTIFY tracksChanged)
    Q_PROPERTY(QString tracksError READ tracksError NOTIFY tracksChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
    Q_PROPERTY(int paneWidth READ paneWidth WRITE setPaneWidth NOTIFY paneWidthChanged)

public:
    static constexpr int kDefaultPaneWidth = 320;
    static constexpr int kMinPaneWidth = 200;
    static constexpr int kMaxPaneWidth = 640;

    /// settings 為 nullptr 時不保存左欄寬度；checkDisk 為 false 時不檢查資料夾在不在（測試用）。
    FoldersController(LibraryQuery& library, PlaybackService& playback, ModuleSettings* settings,
                      bool checkDisk = true, QObject* parent = nullptr);
    /// 等工作執行緒結束（同 shutdown()）。
    ~FoldersController() override;

    FoldersModel* tree() { return &m_tree; }
    SearchResultsModel* tracks() { return &m_tracks; }
    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    int selectedRow() const { return m_tree.rowOfFolder(m_selectedId); }
    QString selectedPath() const;
    bool selectedMissing() const;
    qint64 selectedFolderId() const { return m_selectedId; }
    bool tracksLoaded() const { return m_tracksLoaded; }
    QString tracksError() const { return m_tracksError; }
    int trackCount() const { return m_tracks.rowCount(); }
    int paneWidth() const { return m_paneWidth; }
    void setPaneWidth(int width);

    /// 第一次顯示：還沒讀過才讀。
    Q_INVOKABLE void load();
    /// 重讀資料夾樹（掃描中定時、錯誤的「重試」），之後重讀所選資料夾的曲目。
    Q_INVOKABLE void reload();
    /// 選左欄第 row 列並讀它的曲目；不在範圍時不做事。
    Q_INVOKABLE void select(int row);
    /// 重讀所選資料夾的曲目（曲目讀取失敗時的「重試」）。
    Q_INVOKABLE void reloadTracks();
    /// 展開、收合第 row 列（樹的右鍵、左鍵、點箭頭）；沒有變化時回傳 false。
    Q_INVOKABLE bool expand(int row);
    Q_INVOKABLE bool collapse(int row);
    /// 父資料夾的列（左鍵在收合的資料夾上：移到父資料夾）；根是 -1。
    Q_INVOKABLE int parentRow(int row) const { return m_tree.parentRow(row); }
    /// 佇列換成右邊整份曲目，從第 row 首開始播（雙擊或 Enter）；不在範圍時回傳 false。
    Q_INVOKABLE bool playFrom(int row);

    /// 模組停止時：丟掉排隊中的讀取、等正在讀的結束。之後不再更新。
    void shutdown();

    static void setInstance(FoldersController* instance);
    static FoldersController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();
    void selectionChanged();
    void tracksChanged();
    void paneWidthChanged();

private:
    void onTreeLoaded(quint64 generation, QList<FolderNode> nodes, const QString& error);
    void onTracksLoaded(quint64 generation, QList<TrackInfo> tracks, const QString& error);
    const FolderNode* selectedNode() const;

    LibraryQuery& m_library;
    PlaybackService& m_playback;
    ModuleSettings* m_settings;
    bool m_checkDisk;
    FoldersModel m_tree;
    SearchResultsModel m_tracks;
    bool m_loaded = false;
    bool m_busy = false;
    bool m_reloadPending = false;
    QString m_error;
    qint64 m_selectedId = 0;
    bool m_tracksLoaded = false;
    QString m_tracksError;
    int m_paneWidth = kDefaultPaneWidth;
    std::shared_ptr<std::atomic<quint64>> m_treeGeneration =
        std::make_shared<std::atomic<quint64>>(0);
    std::shared_ptr<std::atomic<quint64>> m_tracksGeneration =
        std::make_shared<std::atomic<quint64>>(0);
    // 最後宣告、最先解構：解構時先等讀取結束
    QThreadPool m_pool;
};

} // namespace auralis::mod::lib
