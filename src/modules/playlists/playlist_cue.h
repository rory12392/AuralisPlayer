#pragma once

// m3u／m3u8／pls 裡的 .cue 路徑展開成整張 CUE 專輯，以及重新檢查時向音樂庫確認 CUE 曲目（T-56）。
// 不自己解析 .cue：軌號一律向音樂庫查（LibraryQuery::tracks），身分照核心的 (.cue 路徑, 軌號)。

#include "playlist_parse.h"

#include <auralis/library_query.h>
#include <auralis/track_ref.h>

#include <QList>
#include <QString>

#include <functional>

namespace auralis::mod::pl {

/// 匯入與重新檢查時向音樂庫查的兩件事。沒設的欄位＝不查（照 T-52 只看磁碟的規則）。
struct LibraryCheck {
    /// 這個 .cue 在音樂庫裡的全部軌號（由小到大，同一軌只列一次）；沒有時空的。
    std::function<QList<quint32>(const QString& cuePath)> cueTracks;
    /// 這個身分在不在音樂庫（findByIdentity）。
    std::function<bool(const TrackRef& ref)> contains;
};

/// 整張 CUE 的項目：路徑是 .cue、subsong 0（m3u 匯出的整張專輯；音樂庫沒有這個身分）。任何執行緒。
bool isWholeCue(const TrackRef& ref);
/// 內嵌 cue 在音樂庫對不到時存的項目（T-53 的整軌檔＋軌號）：
/// subsong 不是 0、路徑不是 .cue。任何執行緒。
bool isEmbeddedCueTrack(const TrackRef& ref);
/// 兩種項目找不到時給使用者看的原因（S-09）；其他項目回傳空字串。任何執行緒。
QString cueMissingReason(const TrackRef& ref);

/// 匯入：整張 CUE 的項目換成音樂庫裡這個 .cue 的全部軌
/// （.cue 路徑＋軌號，依軌號排序），插在原本那一筆的位置
/// （同一個 .cue 出現兩次就展開兩次）。音樂庫沒有這個 .cue 的曲目
/// （或沒給 cueTracks）時保留 (.cue, 0) 並寫 unmatched（之後算找不到）。
/// 被路徑規則拒絕的不查；沒有整張 CUE 的項目時不呼叫 cueTracks。工作執行緒。
void expandCueEntries(QList<ImportEntry>& entries, const LibraryCheck& library);

/// 用音樂庫查：cueTracks 第一次被呼叫時讀一次全部曲目，依 pathKey 分出各 .cue 的軌號；contains 用
/// findByIdentity。回傳的函式在工作執行緒呼叫（一份只給一個工作用），library 要活得比它久。
LibraryCheck libraryCheck(const LibraryQuery& library);

} // namespace auralis::mod::pl
