#pragma once

#include <auralis/library_query.h>
#include <auralis/notifier.h>
#include <auralis/playback_service.h>
#include <auralis/result.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace auralis::ui {

/// 佇列面板一列要顯示的曲目資訊。
struct QueueTrackMeta {
    QString title;
    QString artist;
    /// 不知道時 -1。
    qint64 durationMs = -1;
    /// 封面的 key（CoverService），空字串＝沒有封面。
    QString coverKey;
};

/// 依 TrackRef 查曲目資訊（主程式接 LibraryQuery::findByIdentity）。在工作執行緒呼叫，
/// 可能同時被呼叫；音樂庫裡沒有這首時回傳 std::nullopt（畫面改用檔名）。
using QueueMetaLookup = std::function<std::optional<QueueTrackMeta>(const TrackRef&)>;

/// 只帶查詢條件的 TrackDragPayload.query → 曲目（主程式接
/// LibraryQuery::tracksForDragQuery；T-62）。
/// 在工作執行緒呼叫；不是查詢條件或資料庫錯誤時回傳錯誤。
using DragQueryResolver = std::function<Result<QList<TrackInfo>>(const QString& query)>;

/// 佇列面板（S-13）的清單：PlaybackService 的佇列從目前曲目開始的部分（第 0 列是「正在播放」，
/// 其餘是「接下來」；沒有目前曲目時全部是「接下來」）。目前曲目之前已播過的項目不列出。
/// 佇列一改變就整個重建；曲名、藝人、長度在背景向音樂庫查，查到後更新。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 取用的型別，型別註冊會繼承它，所以不能標 final
class QueueModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 Player.queue 提供")
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool hasCurrent READ hasCurrent NOTIFY changed)
    /// 列出的曲目的總長（毫秒），不知道長度的不算。
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY changed)

public:
    enum Role {
        EntryIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        DurationMsRole,
        CurrentRole,
        UnplayableRole,
        /// image://cover/<key>；沒有封面時空字串。
        CoverUrlRole,
    };

    /// notifier 為 nullptr 時清空佇列不提供復原（測試用）。
    QueueModel(PlaybackService& service, Notifier* notifier, QueueMetaLookup lookup,
               QObject* parent = nullptr);
    ~QueueModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    bool hasCurrent() const { return m_hasCurrent; }
    qint64 totalDurationMs() const;
    /// 查過的資訊；還沒查到或音樂庫沒有時用檔名。
    QueueTrackMeta meta(const TrackRef& track) const;

    /// 雙擊：跳到這一列開始播。
    Q_INVOKABLE void playAt(int row);
    /// Delete：移出佇列。
    Q_INVOKABLE void removeAt(int row);
    /// 拖曳或鍵盤排序：把第 from 列移到第 to 列。「正在播放」那列不動，其他列也不能移到它前面。
    Q_INVOKABLE void moveTo(int from, int to);
    /// 從清單拖來的曲目（TrackDragPayload 的 JSON）插在第 row 列的位置（row ＝ count 時放最後）。
    /// 只帶查詢條件時在工作執行緒取曲目，取到後才插入（沒設 setDragQueryResolver 時不接受）。
    /// 回傳是否接受（不是 TrackDragPayload 時 false）。
    Q_INVOKABLE bool insertTracks(const QString& json, int row);
    /// 只帶查詢條件的內容怎麼取曲目；沒設時這種內容一律不接受（T-62）。
    void setDragQueryResolver(DragQueryResolver resolver) { m_resolver = std::move(resolver); }
    /// json（TrackDragPayload）的曲目交給 apply，在 UI 執行緒呼叫：有列出曲目時馬上呼叫；
    /// 只帶查詢條件時在工作執行緒取曲目，取完回到 UI 執行緒再呼叫（context 已經刪掉、
    /// 取到 0 首時不呼叫；取不到時通知列顯示錯誤）。回傳是否接受。Player 的右鍵項目也用它。
    bool withTracks(const QString& json, QObject* context,
                    std::function<void(const QList<TrackRef>&)> apply);
    /// 「清空佇列」：正在播的那首留著，其他移出；通知列提供「復原」（把「接下來」的曲目照原順序加回最後）。
    Q_INVOKABLE void clearQueue();

    /// 測試用：等背景查詢（含依查詢條件取曲目）做完並套用結果。
    void waitForLookups();

signals:
    void changed();
    /// 查到新的曲目資訊（播放列的曲名與藝人跟著更新）。
    void metaChanged();

private:
    void rebuild();
    void requestMeta();
    void applyMeta(const QHash<QString, QueueTrackMeta>& found);
    void placeTracks(const QList<TrackRef>& tracks, int row);
    int queueIndex(int row) const { return m_start + row; }

    PlaybackService& m_service;
    Notifier* m_notifier;
    QueueMetaLookup m_lookup;
    QList<QueueEntry> m_rows;
    int m_start = 0;
    bool m_hasCurrent = false;
    QHash<QString, QueueTrackMeta> m_cache;
    QSet<QString> m_pending;
    DragQueryResolver m_resolver;
    // 最後宣告、最先解構：解構時先等背景查詢結束，結果才不會送到已拆掉的物件
    QThreadPool m_pool;
    // 依查詢條件取曲目（一次可能 4 萬首）另用一條執行緒：不排在逐首查資訊後面，且依序套用
    QThreadPool m_resolvePool;
};

} // namespace auralis::ui
