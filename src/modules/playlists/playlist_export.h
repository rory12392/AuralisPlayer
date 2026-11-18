#pragma once

#include "playlist_store.h"

#include <auralis/result.h>

#include <QByteArray>
#include <QList>
#include <QString>

namespace auralis {
class LibraryQuery;
}

namespace auralis::mod::pl {

/// 匯出檔裡的一首（T-54，US-14）。
struct ExportLine {
    /// 要寫的路徑：完整本機路徑（CUE 曲目換成整軌檔）；verbatim 時是存著的原文。
    QString path;
    /// #EXTINF 的秒數；音樂庫找不到時 -1。
    int seconds = -1;
    /// #EXTINF 的「藝人 - 曲名」；音樂庫找不到時是檔名。
    QString title;
    /// 匯入時被路徑規則拒絕、照原文存著的項目（例：\\NAS\…）：照原文寫，不換成相對路徑。
    bool verbatim = false;
};

/// 匯出內容（還沒決定路徑寫法）。
struct ExportContent {
    QList<ExportLine> lines;
    /// 有 CUE 曲目換成整軌檔的路徑（完成通知要加一句提醒，m3u 無法表示區段）。
    bool hasCue = false;
    /// 路徑含控制字元、沒有匯出的首數（buildM3u8 改寫成一行 # 註解；M-04 審查）。
    int invalidPaths = 0;
};

/// 播放清單項目 → 匯出的每一行。秒數、藝人與曲名取自音樂庫（findByIdentity）；
/// 找不到的照存著的路徑、#EXTINF:-1,<檔名>；CUE 曲目用 audioFilePath 換成整軌檔的路徑。
/// 被拒絕、照原文存著的項目（storedVerbatim）不查音樂庫、照原文寫（不因匯出而連網）。
/// library 是 nullptr 時全部當成找不到。執行緒：工作執行緒（會讀資料庫與 .cue）。
ExportContent exportLines(const QList<PlaylistItem>& items, const LibraryQuery* library);

/// 匯出時寫的路徑：absolute＝完整路徑；relative＝相對於 exportDir（匯出檔所在資料夾，
/// 例：..\音樂\a.flac）。不同磁碟機、或相對路徑讀回來會變（# 開頭、前後有空白）時寫完整路徑。
/// 只做字串處理。任何執行緒。
QString exportPathFor(const QString& path, const QString& exportDir, bool relative);

/// .m3u8 的內容：UTF-8 無 BOM、第一行 #EXTM3U、每首 #EXTINF:<秒>,<標題> 接路徑，換行 CRLF。
/// 路徑含控制字元的那一首只寫一行「#路徑含無效字元，沒有匯出：<路徑>」（控制字元換成 U+FFFD）。
/// 任何執行緒。
QByteArray buildM3u8(const QList<ExportLine>& lines, const QString& exportDir, bool relative);

/// 寫使用者選的那一個檔：先寫同資料夾的暫存檔，全部寫完再改名取代（QSaveFile）；
/// 失敗時原檔不動、暫存檔不留。執行緒：工作執行緒。
Result<bool> writeExportFile(const QString& target, const QByteArray& bytes);

} // namespace auralis::mod::pl
