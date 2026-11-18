#pragma once

#include <auralis/result.h>
#include <auralis/track_ref.h>

#include <QList>
#include <QString>

#include <optional>

namespace auralis {
class ModuleDatabase;
} // namespace auralis

namespace auralis::mod::pl {

/// 一個播放清單的摘要（側欄與右鍵選單用）。
struct PlaylistInfo {
    qint64 id = 0;
    QString name;
    int trackCount = 0;
};

/// 播放清單裡的一筆。pos 從 0 起、連續；同一首可以出現好幾次（每次一筆）。
struct PlaylistItem {
    int pos = 0;
    TrackRef track;
    bool missing = false;
};

/// 一列的路徑更新（T-52 重新對應與重新檢查）：只在那一列目前的路徑還是 oldPath 時寫。
struct ItemUpdate {
    int pos = 0;
    QString oldPath;
    /// 檢查時那一列的 subsong：同一個檔的別軌換到這個位置時不寫（M-04 審查）。
    quint32 subsong = 0;
    QString newPath;
    bool missing = false;
};

/// 一列換成好幾首（T-56：整張 CUE 的 (.cue, 0) 展開成各軌）：只在那一列目前還是 track 時換。
struct ItemExpansion {
    int pos = 0;
    TrackRef track;
    QList<TrackRef> tracks;
    bool missing = false;
};

/// path_remaps 的一條對應規則（T-52）。
struct PathRemap {
    QString oldRoot;
    QString newRoot;
};

/// 本模組對 playlists、playlist_items、path_remaps 的全部讀寫（資料庫寫入集中在一個 Repository
/// 類別）。 曲目一律存身分 (path, subsong) 與 path_key，不存 tracks.id，重新掃描後照樣對得上。
/// 會改到好幾列的操作（插入、移除、重排、刪除）都在一個交易裡做完，中途失敗或程式關閉不會留下半套。
/// 執行緒：任何執行緒（ModuleDatabase 會讓同一個模組的呼叫依序執行）。
class PlaylistStore {
public:
    explicit PlaylistStore(ModuleDatabase& db);

    /// 依側欄順序（sort_order、id）列出全部播放清單與曲目數。
    Result<QList<PlaylistInfo>> list() const;
    /// 讀一個播放清單；不存在時回傳 std::nullopt。
    Result<std::optional<PlaylistInfo>> playlist(qint64 id) const;
    /// 依 pos 順序讀全部項目。
    Result<QList<PlaylistItem>> items(qint64 id) const;

    /// 新增空的播放清單，排在最後；回傳新的 id。名稱前後空白去掉，空白名稱是錯誤。
    Result<qint64> create(const QString& name);
    /// 匯入（T-51）：新增播放清單並寫入全部項目（含 missing），一個交易做完，失敗時什麼都不留。
    /// 名稱已經有了就加「 (2)」「 (3)」…（§1 情況 4）。回傳新的 id 與實際用的名稱。
    Result<PlaylistInfo> createImported(const QString& name, const QList<PlaylistItem>& items);
    /// 改名；名稱規則同 create。回傳是否有這個播放清單。
    Result<bool> rename(qint64 id, const QString& name);
    /// 刪除播放清單與它的項目（只動資料庫，不碰任何音樂檔，AC-11-3）。回傳是否有這個播放清單。
    Result<bool> remove(qint64 id);

    /// 加入曲目：insertAt 是插入位置（0＝最前面），小於 0 或超過曲目數時加在最後。
    /// 回傳是否有這個播放清單。
    Result<bool> addTracks(qint64 id, const QList<TrackRef>& tracks, int insertAt = -1);
    /// 移除這些位置的項目（重複、不存在的位置略過），之後的項目往前補，pos 維持連續。
    Result<bool> removeItems(qint64 id, const QList<int>& positions);
    /// 重排：order[i] 是新順序第 i 筆原本的 pos，一定要是 0..n-1 的一個排列，否則不改並回傳錯誤。
    /// 一個交易寫完（拖曳排序放開時呼叫，中途關閉程式不會半套）。
    Result<bool> reorder(qint64 id, const QList<int>& order);

    /// 改這些列的路徑與 missing（T-52），一個交易；那一列的路徑已經不是 oldPath（期間被移除、
    /// 重排或改過）時略過那一列。回傳實際寫入的列數。
    Result<int> updateItems(qint64 id, const QList<ItemUpdate>& updates);
    /// 把這些列各自換成好幾首（T-56），一個交易；那一列已經不是 track
    /// （路徑或 subsong 不同）時略過。之後的項目往後移，pos 維持連續。回傳實際換掉的列數。
    Result<int> expandItems(qint64 id, const QList<ItemExpansion>& expansions);
    /// 存過的對應規則，最新的在前面。
    Result<QList<PathRemap>> remaps() const;
    /// 存一條對應規則；原本的資料夾相同（pathKey）的舊規則換成這一條。
    Result<bool> saveRemap(const PathRemap& remap);

private:
    /// 用 items 取代整個清單的項目（同一個交易內呼叫）。
    bool rewriteItems(qint64 id, const QList<PlaylistItem>& items);
    bool touch(qint64 id);

    ModuleDatabase& m_db;
};

} // namespace auralis::mod::pl
