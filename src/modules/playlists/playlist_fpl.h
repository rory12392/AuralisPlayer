#pragma once

// foobar2000 .fpl 匯入（T-53，US-13 AC-1–3）。
// .fpl 是外部輸入的二進位檔：所有位移、長度、計數先和剩下的位元組數比過才讀，
// 任何一項不符就整份視為「不支援此 .fpl 版本」，不建立殘缺的播放清單（AC-13-3）。

#include "playlist_parse.h"

#include <auralis/library_query.h>
#include <auralis/result.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace auralis::mod::pl {

/// .fpl 單檔讀取上限（Architecture §8「單次讀取有上限」；和核心 TextDecoder 的文字檔上限相同）。
inline constexpr qint64 kMaxFplBytes = 32 * 1024 * 1024;
/// .fpl 的筆數上限：結構合法但筆數大到記憶體峰值過高（每筆先配置 ImportEntry）的檔視為不支援。
inline constexpr qsizetype kMaxFplEntries = 200'000;

/// 唯讀開啟（共用讀寫刪除，AC-12-4）讀整個 .fpl。不存在、被鎖住、超過 kMaxFplBytes 時回傳 Error
/// （結果畫面的「讀不到」）。工作執行緒。
Result<QByteArray> readFplFile(const QString& path);

/// 解析 .fpl 的內容。結構不符（標頭、字串表、任何位移或長度越界、entries 長度
/// E != 4 + 2*pk + nvals + 2*sk、最後一筆沒有剛好到檔尾、空檔）時回傳 std::nullopt。
/// 每一筆：
/// - 路徑去掉 file:// 後照 resolveEntryPath 的規則（網路路徑、其他協定、相對路徑被拒絕，
///   raw 是去掉 file:// 的原文）；
/// - subsong：路徑是 .cue 時是軌號；次要欄位 cue_embedded=yes 時是軌號並標 embeddedCue；
///   其他一律 0（DSD 檔的 1 不代表第 1 軌）；
/// - label：「藝人 - 曲名」（只有曲名時是曲名；多值用「; 」接起來），找不到的清單顯示用。
/// 任何執行緒（純位元組處理）。
std::optional<QList<ImportEntry>> parseFpl(const QByteArray& bytes);

/// 內嵌 cue 的對應：整軌檔 audioFile 的第 track 軌在音樂庫裡的 CUE 曲目身分；沒有時 std::nullopt。
using CueLookup = std::function<std::optional<TrackRef>(const QString& audioFile, quint32 track)>;

/// 把 embeddedCue 的項目換成音樂庫 CUE 曲目的身分（.cue 路徑＋軌號）；找不到（或沒有 lookup）時
/// 保留整軌檔路徑＋軌號，unmatched 寫原因（之後算找不到）。被路徑規則拒絕的不查。工作執行緒。
void matchEmbeddedCue(QList<ImportEntry>& entries, const CueLookup& lookup);

/// 用音樂庫查內嵌 cue：第一次被呼叫時讀一次全部曲目，挑 .cue 曲目裡軌號相同、整軌檔在 .cue
/// 所在資料夾底下的，再用 LibraryQuery::audioFilePath（核心 T-77）確認整軌檔就是 audioFile。
/// 不自己解析 .cue。回傳的函式在工作執行緒呼叫，library 要活得比它久。
CueLookup libraryCueLookup(const LibraryQuery& library);

} // namespace auralis::mod::pl
