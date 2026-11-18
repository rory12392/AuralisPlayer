#pragma once

#include "playlist_cue.h"
#include "playlist_parse.h"
#include "playlist_store.h"

#include <auralis/result.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace auralis::mod::pl {

/// 找不到的檔案與重新對應（T-52，US-12 AC-3、US-13 AC-4；畫面規格 S-08、S-09）：
/// 字串處理（共同前綴、套用對應規則）與在工作執行緒做的重新檢查。
/// 只改 playlist_items 的路徑與 missing，不碰播放清單檔與音樂檔（AC-12-4）。

/// 這些路徑的共同資料夾（S-09「原本的資料夾」的預填值）：一層一層比，大小寫與分隔符不同也算同一層
/// （pathKey）；沒有共同的一層時空字串。結果用第一個路徑的原文、Windows 分隔符、結尾不帶 \。
/// 任何執行緒。
QString commonFolder(const QStringList& paths);

/// path 在 root 底下時，回傳 root 以下的各層（原文）；不在底下（或就是 root 本身）時 std::nullopt。
/// 比對同 commonFolder（pathKey 一層一層比）。任何執行緒。
std::optional<QStringList> partsUnder(const QString& path, const QString& root);

/// 套用「原本的資料夾 → 現在的資料夾」：path 在 oldRoot 底下時回傳 newRoot＋以下的各層。
/// - path 不在 oldRoot 底下：std::nullopt；
/// - 以下的各層有 . 或 ..（會跳出現在的資料夾）、或結果被路徑規則拒絕（網路路徑、裝置名稱、
///   替代資料流，resolveEntryPath）：Error（給使用者看的原因）。
/// 只做字串處理，不查檔案系統。任何執行緒。
std::optional<Result<QString>> remapPath(const QString& path, const QString& oldRoot,
                                         const QString& newRoot);

/// 找不到的項目給使用者看的原因：被路徑規則拒絕的寫拒絕原因，其他寫「找不到檔案」。任何執行緒。
QString missingReason(const QString& path);

/// 一個播放清單的找不到的項目（S-09 的讀數與清單）。
struct MissingView {
    int total = 0;
    /// [{path, reason}]，照清單順序
    QList<QPair<QString, QString>> missing;
};
MissingView missingView(const QList<PlaylistItem>& items);

/// 找不到的項目裡要另外寫明原因的（S-1：經過符號連結或 junction）：路徑 → 原因。被路徑規則拒絕的
/// 不查；reason 空的時回傳空的。工作執行緒（reason 會查檔案系統）。
QHash<QString, QString> linkReasons(const QList<PlaylistItem>& items, const ReasonCheck& reason);

/// 重新對應＋重新檢查一個播放清單（S-09「套用並重新檢查」、S-08「重新對應資料夾」）：
/// - 找不到的項目在 oldRoot 底下：換成 newRoot 底下的路徑，用 exists（起點＝newRoot，
///   起點本身不檢查）查到了才寫入新路徑並清掉 missing；查不到或被拒絕的維持原路徑與 missing；
/// - 其他找不到的項目：原路徑重新查一次（起點空＝從磁碟機根目錄），被拒絕的不查詢；
/// - oldRoot 空的時候只做重新檢查。
/// 寫入是一個交易，只寫有變的列，而且只在那一列的路徑沒被別人改過時寫（PlaylistStore::updateItems）。
/// 對應規則有找到任何一首時存進 path_remaps（下次匯入自動建議）。回傳更新後的項目。
/// 整張 CUE 與內嵌 cue 的項目（T-56）「找到」以 library 為準（不看磁碟），
/// 整張 CUE 找到時展開成各軌。執行緒：工作執行緒（會查磁碟）。
struct RemapOutcome {
    QList<PlaylistItem> items;
    /// 經對應規則找到的曲目數
    int remapped = 0;
};
Result<RemapOutcome> remapPlaylist(PlaylistStore& store, qint64 id, const QString& oldRoot,
                                   const QString& newRoot, const ExistsCheck& exists,
                                   const LibraryCheck& library = {});

/// 所有播放清單的 missing 重新檢查（§1 情況 2：音樂庫掃描完成後）：每一列用 exists 查，
/// 被路徑規則拒絕的（網路路徑等）不查詢、維持原本的 missing；在存過的對應規則的「現在的資料夾」
/// 底下的，以那個資料夾為起點。回傳 missing 有變的播放清單 id。
/// T-56：整張 CUE（(.cue, 0)）與內嵌 cue 對不到的項目（整軌檔＋軌號）以 library 為準、不看磁碟：
/// 整張 CUE 在音樂庫已經有這個 .cue 的曲目時展開成各軌（各軌的 missing 看 .cue 在不在磁碟上），
/// 沒有時標 missing；內嵌 cue 的項目音樂庫有這個身分才清掉 missing。library 沒給的欄位照只看磁碟。
/// 執行緒：工作執行緒（會查磁碟；4 萬首量過的時間見 T-52 回報）。
Result<QList<qint64>> recheckAll(PlaylistStore& store, const ExistsCheck& exists,
                                 const LibraryCheck& library = {});
/// recheckAll 的一個播放清單（播放清單頁開「重新對應資料夾」前也先做，讓清單和磁碟一致）；
/// remaps 是 PlaylistStore::remaps()。回傳 missing（或展開後的項目）是否有變。執行緒：工作執行緒。
Result<bool> recheckPlaylist(PlaylistStore& store, qint64 id, const QList<PathRemap>& remaps,
                             const ExistsCheck& exists, const LibraryCheck& library = {});

/// 查 path 時的起點：在存過的對應規則的「現在的資料夾」底下時是那個資料夾，否則空的。任何執行緒。
QString startFor(const QString& path, const QList<PathRemap>& remaps);
/// path 在磁碟上（被路徑規則拒絕的不查詢，算不在）。執行緒：工作執行緒（會查磁碟）。
bool existsOnDisk(const QString& path, const QList<PathRemap>& remaps, const ExistsCheck& exists);

/// 存過的對應規則裡，和這些找不到的路徑對得上的（舊資料夾是其中一個路徑的上層），最新的優先；
/// 沒有時 std::nullopt。任何執行緒。
std::optional<PathRemap> suggestRemap(const QList<PathRemap>& remaps, const QStringList& missing);

} // namespace auralis::mod::pl
