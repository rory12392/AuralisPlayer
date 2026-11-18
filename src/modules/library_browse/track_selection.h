#pragma once

#include <auralis/library_query.h>

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace auralis::mod::lib {

/// 音樂庫清單的選取（T-25，DESIGN §7 清單的鍵盤操作）與它代表的曲目：拖放（TrackDragArea）
/// 和右鍵選單（TrackMenu）用同一份 TrackDragPayload。每個清單在 QML 建立一個，model 給
/// SearchResultsModel（一列一首）或 AlbumsModel（一列一張專輯，代表整張專輯的曲目）。
/// - 選取的操作：select（只選這列）、toggle（Ctrl＋點擊）、extend（Shift＋上下鍵：從錨點到這列）、
///   selectAll（Ctrl+A；之後分批接上的列也算選取）、click（依目前按住的 Ctrl／Shift 決定）。
/// - 模型重設或中間插入、移除列時清掉選取；只在最後接上新列（搜尋分批載入）時保留。
/// - 超過 TrackDragPayload::kMaxTracks 首時只帶查詢條件（query()）：全選（selectAll，或已全部載入時
///   選滿）時用頁面給的 allQuery，
///   專輯清單用選取的 albumIds；曲目清單只選一部分時沒有條件可代表，照樣列出曲目。
/// 執行緒：只在 UI 執行緒使用。
class TrackSelection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)
    /// 全選時代表整份清單的查詢條件（toDragQuery 的字串；空字串＝沒有）。
    Q_PROPERTY(QString allQuery READ allQuery WRITE setAllQuery NOTIFY allQueryChanged)
    /// 清單已全部載入（搜尋分批載入時頁面綁 LibrarySearch.complete）。還沒載完時只有 selectAll
    /// 代表整份清單；手動選滿已載入的列不算，免得帶到還沒載入、沒被選的曲目。
    Q_PROPERTY(bool complete READ complete WRITE setComplete NOTIFY changed)
    /// 選取的列數。
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// 選取的曲目數（專輯清單是各專輯曲數的和）。
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)
    /// 選取每變一次加一：清單列的 highlighted 綁定它才會重算。
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// 只帶查詢條件（曲目超過 5,000 首而且有條件可代表）。
    Q_PROPERTY(bool queryOnly READ queryOnly NOTIFY changed)

public:
    explicit TrackSelection(QObject* parent = nullptr);

    QAbstractItemModel* model() const { return m_model; }
    void setModel(QAbstractItemModel* model);
    QString allQuery() const { return m_allQuery; }
    void setAllQuery(const QString& query);
    bool complete() const { return m_complete; }
    void setComplete(bool complete);
    int count() const { return m_count; }
    int trackCount() const { return m_trackCount; }
    int revision() const { return m_revision; }
    bool queryOnly() const;

    Q_INVOKABLE bool isSelected(int row) const;
    /// 只選這一列，錨點移到這列。
    Q_INVOKABLE void select(int row);
    /// 切換這一列（Ctrl＋點擊），錨點移到這列。
    Q_INVOKABLE void toggle(int row);
    /// 選取從錨點到這一列（Shift＋上下鍵、Shift＋點擊）；沒有錨點時同 select。
    Q_INVOKABLE void extend(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clear();
    /// 滑鼠點擊：依目前按住的鍵（Ctrl → toggle、Shift → extend、都沒有 → select）。
    Q_INVOKABLE void click(int row);
    /// 右鍵與拖曳：這列沒被選取時改成只選它（已選取時保留整個選取）。
    Q_INVOKABLE void ensureSelected(int row);

    /// 這一列代表的曲目數（曲目清單 1、專輯清單是曲數）。
    Q_INVOKABLE int rowTrackCount(int row) const;
    /// 選取的曲目 [{path, subsong}]，依列的順序（專輯展開成曲目）。queryOnly 時不用叫它。
    Q_INVOKABLE QVariantList tracks() const;
    /// 超過 5,000 首時的查詢條件（見類別說明）；不需要時空字串。
    Q_INVOKABLE QString query() const;
    /// 右鍵選單用的 TrackDragPayload JSON（和拖放同一份）。只有一首時順便記下它的專輯與藝人，
    /// 給「前往專輯」「前往藝人」（trackHint）。
    Q_INVOKABLE QString payloadJson() const;

    /// 搜尋與篩選條件的查詢字串（所有曲目頁全選時的 allQuery）。
    Q_INVOKABLE QString searchQuery(const QString& text, const QString& artist,
                                    const QString& album, int year, const QString& format,
                                    int sampleRate) const;
    /// 資料夾（含子資料夾）的查詢字串（資料夾頁全選時的 allQuery）；folderId 0 時空字串。
    Q_INVOKABLE QString folderQuery(qint64 folderId) const;
    /// 一張專輯的查詢字串（專輯詳細頁全選時的 allQuery）；albumId 0 時空字串。
    Q_INVOKABLE QString albumQuery(qint64 albumId) const;

    /// 選取的曲目（C++ 與測試用）。
    QList<TrackInfo> selectedTracks() const;
    /// 選取的列號，由小到大。
    QList<int> selectedRows() const;

    /// 最近一次 payloadJson() 只有一首時的曲目（專輯 id、藝人）；沒有時 ref.path 是空的。
    static const TrackInfo& trackHint();

signals:
    void modelChanged();
    void allQueryChanged();
    void changed();

private:
    void resize(bool keepAll);
    void setRange(int from, int to);
    void finish();
    int rowCount() const;

    QPointer<QAbstractItemModel> m_model;
    QString m_allQuery;
    std::vector<char> m_selected;
    int m_count = 0;
    int m_trackCount = 0;
    int m_revision = 0;
    int m_anchor = -1;
    bool m_all = false;
    bool m_complete = true;
};

} // namespace auralis::mod::lib
