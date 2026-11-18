#pragma once

#include "library/file_enumerator.h"

#include <auralis/library_query.h>
#include <auralis/result.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace auralis::core {

/// 使用者直接給的路徑（拖進視窗、命令列）展開後的結果。
struct OpenPathsResult {
    /// 照給的順序；資料夾展開成裡面的曲目（依路徑自然排序），.cue 展開成它的各軌。
    QList<TrackRef> tracks;
    /// 開不了的項目與原因（給使用者看的文字，AC-01-3 的句型由呼叫端組）。
    QList<ScanFailure> skipped;
};

/// 使用者給的路徑只做字串檢查（不查詢檔案系統）：必須是完整的本機路徑（C:\…）或網路路徑
/// （\\主機\共用\…）。帶協定前綴（http:、file:、concat: 這類「名稱:」）、\\?\ 與 \\.\ 開頭、
/// 替代資料流（磁碟機代號以外的冒號）、CON／NUL 這類裝置名稱一律拒絕。
/// 成功時回傳 Windows 分隔符的路徑。任何執行緒。
Result<QString> checkUserPath(const QString& path);

/// 命令列的參數 → 完整路徑：相對路徑接在 baseDir（啟動時的目前資料夾）後面；
/// 帶協定前綴或已是完整路徑的原樣回傳（交給 checkUserPath 判斷）。只做字串處理。任何執行緒。
QString absoluteUserPath(const QString& argument, const QString& baseDir);

/// 把使用者給的檔案與資料夾展開成要放進佇列的曲目（AC-11-5），不需要先加入音樂庫：
/// - 資料夾：遞迴列出音樂檔與 .cue（和音樂庫掃描同一套規則：副檔名在音樂庫收錄的格式、
///   不進入重新解析點；裡面的符號連結與 junction 列進
///   skipped），依路徑自然排序（每一層分開比，數字照數值）；
/// - .cue：照 CUE 曲目的規則展開成各軌，它涵蓋的整軌檔不另外加入（這次給的所有項目都算）；
///   讀不到或找不到整軌檔時列進 skipped，整軌檔照常以整檔加入；
/// - 其他檔案：副檔名是音樂庫收錄的格式才加入，否則列進 skipped。
/// 只讀目錄與 .cue，不開音樂檔、不改任何東西。執行緒：工作執行緒（資料夾大或在網路上時很慢）。
OpenPathsResult expandOpenPaths(const QStringList& paths);

} // namespace auralis::core
