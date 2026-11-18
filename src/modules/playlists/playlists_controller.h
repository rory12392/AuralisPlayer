#pragma once

#include "playlist_store.h"

#include <auralis/job_gate.h>
#include <auralis/library_query.h>
#include <auralis/track_drag_payload.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class Dialogs;
class JobRunner;
class Notifier;
class PlaybackService;
} // namespace auralis

namespace auralis::mod::pl {

/// 側欄播放清單區、右鍵「加入播放清單 ▸」與播放清單頁共用的服務，QML 以單例 Playlists 取用
/// （畫面規格 S-02 側欄、S-08；US-11 AC-1、3、4）。
/// - playlists：側欄順序的 [{id, name, count}]，任何改動後重讀。
/// - editingId：側欄正在原地改名的那一列（新增時直接進入改名，S-08）；0＝沒有。
/// - 加入曲目收的是 TrackDragPayload 的 JSON（拖放與右鍵選單同一份）。只帶查詢條件
///   （超過 5,000 首）時在背景工作裡向音樂庫依條件取曲目（會讀資料庫，不在 UI 執行緒），
///   取回後才寫入；這時方法回傳「已接受」，結果看通知與 playlists。
/// - requestRemove 先用確認對話框問（AC-11-3），按了「刪除播放清單」才刪，只動資料庫。
/// - 播放清單頁（T-55）的寫入也經這裡：reorder（拖曳排序放開時一次）、
///   removeItems（通知帶「復原」）、addTracks 的 insertAt；改到項目後發 itemsChanged(id)，
///   頁面的曲目清單靠它重讀。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PlaylistsController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Playlists)
    QML_SINGLETON
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(qint64 editingId READ editingId WRITE setEditingId NOTIFY editingIdChanged)

public:
    /// 依查詢條件取曲目（LibraryQuery::tracksForDragQuery）；在工作執行緒呼叫。
    using QueryResolver = std::function<Result<QList<TrackInfo>>(const QString& query)>;

    /// resolver 或 jobs 沒給時不接受只帶查詢條件的內容；playback 沒給時 play() 不做事。
    PlaylistsController(PlaylistStore& store, Dialogs& dialogs, Notifier& notifier,
                        QueryResolver resolver = {}, JobRunner* jobs = nullptr,
                        QObject* parent = nullptr, PlaybackService* playback = nullptr);

    QVariantList playlists() const { return m_playlists; }
    qint64 editingId() const { return m_editingId; }
    void setEditingId(qint64 id);

    /// 新增空的播放清單（預設名稱「新播放清單」），並讓側欄那一列進入改名；回傳 id，失敗時 0。
    Q_INVOKABLE qint64 createPlaylist();
    /// 新增播放清單並加入 payloadJson 的曲目（拖到空位、右鍵「新播放清單…」），
    /// 同樣進入改名。回傳 id，內容不是曲目或失敗時 0（不留下空的清單）；
    /// 只帶查詢條件時曲目取回後才建立，這時回傳 -1（已接受）。
    Q_INVOKABLE qint64 createWithTracks(const QString& payloadJson);
    /// 把 payloadJson 的曲目加進播放清單：insertAt 是插入位置（播放清單頁拖到清單中間），
    /// 小於 0 時加在最後；通知「已加入 <n> 首到「<名稱>」」。
    /// 回傳是否加入（只帶查詢條件時是否已接受）。
    Q_INVOKABLE bool addTracks(qint64 id, const QString& payloadJson, int insertAt = -1);
    /// 模組停止：之後才開始的背景工作直接結束，等已經開始的結束（最多 timeoutMs，逾時寫日誌）。
    void shutdown(int timeoutMs) noexcept;
    /// 照 order 重排（order[i]＝新順序第 i 筆原本的位置，PlaylistStore::reorder），一次寫入。
    /// 畫面上看得到結果，所以不通知；失敗時錯誤通知。
    bool reorder(qint64 id, const QList<int>& order);
    /// 移除這些位置的項目，通知「已移除 <n> 首」＋「復原」（放回原本的位置）。回傳是否移除。
    bool removeItems(qint64 id, const QList<int>& positions);
    /// 播放整個播放清單（側欄項目右鍵「播放」，S-08）；空的或不存在時回傳 false。
    Q_INVOKABLE bool play(qint64 id);
    /// 從播放清單第 index 首開始播放整個清單（播放清單頁的「播放」、Enter、雙擊）。
    bool playFrom(const QList<TrackRef>& tracks, int index);
    /// 通知「這個播放清單的曲目都找不到」（播放清單頁的「播放」全部被擋時；M-04 審查）。
    void reportNothingPlayable();
    /// 改名；空白名稱不改、回傳 false。成功時結束改名狀態。
    Q_INVOKABLE bool rename(qint64 id, const QString& name);
    /// 確認後刪除（AC-11-3）。確認對話框是非同步的，結果看 playlistRemoved 訊號。
    Q_INVOKABLE void requestRemove(qint64 id);
    /// {exists, id, name, count}：播放清單頁用；不存在時 exists=false。
    Q_INVOKABLE QVariantMap info(qint64 id) const;

    /// 不經確認直接刪除（requestRemove 確認後呼叫；測試也用）。
    bool remove(qint64 id);
    /// 重讀播放清單清單。
    void reload();

    /// 模組 start() 時設定、stop() 之後清掉；QML 引擎取單例時用它。
    static void setInstance(PlaylistsController* instance);
    static PlaylistsController* create(QQmlEngine*, QJSEngine*);

signals:
    void playlistsChanged();
    void editingIdChanged();
    void playlistRemoved(qint64 id);
    /// 這個播放清單的項目改了（加入、移除、復原、重排）。
    void itemsChanged(qint64 id);

private:
    /// 解析拖放內容；不是曲目清單時 std::nullopt。
    std::optional<TrackDragPayload> parsePayload(const QString& payloadJson) const;
    /// 只帶查詢條件：在背景工作取曲目，回到 UI 執行緒後交給 apply。回傳是否已排入。
    bool resolveLater(const QString& query, std::function<void(const QList<TrackRef>&)> apply);
    /// 把曲目加進已存在的播放清單（insertAt 同 addTracks）並通知；回傳是否加入。
    bool appendTracks(qint64 id, const QList<TrackRef>& tracks, int insertAt = -1);
    /// 「復原」：把移除的項目依原本的位置放回去（相連的一段一次寫入）。
    void restoreItems(qint64 id, const QList<PlaylistItem>& removed);
    /// 建立新播放清單並加入曲目、進入改名；回傳 id，失敗時 0。
    qint64 createFromTracks(const QList<TrackRef>& tracks);
    QString nameOf(qint64 id) const;
    void reportError(const QString& text, const Error& error);

    PlaylistStore& m_store;
    Dialogs& m_dialogs;
    Notifier& m_notifier;
    QueryResolver m_resolver;
    JobRunner* m_jobs = nullptr;
    /// 背景工作與模組停止之間的閘門（P-PL-24）；工作各持有一份。
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
    PlaybackService* m_playback = nullptr;
    QVariantList m_playlists;
    qint64 m_editingId = 0;
};

} // namespace auralis::mod::pl
