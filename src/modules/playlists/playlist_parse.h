#pragma once

#include <auralis/result.h>

#include <QList>
#include <QString>

#include <functional>

namespace auralis::mod::pl {

/// 播放清單檔裡的一筆（照檔案裡的順序；同一首出現兩次就是兩筆，§1 情況 1）。
struct ImportEntry {
    /// 檔案裡原本寫的文字（去掉前後空白），給找不到的清單顯示。
    QString raw;
    /// 換成的完整本機路徑（Windows 分隔符）；被拒絕時是空的。
    QString path;
    /// 被拒絕的原因（給使用者看）；path 有值時是空的。
    QString rejected;
    /// 曲目身分的 subsong（T-53：.fpl 的 CUE 曲目是軌號；其他一律 0）。
    quint32 subsong = 0;
    /// .fpl 的內嵌 cue（cue_embedded=yes）：path 是整軌檔、subsong 是軌號，
    /// 要向音樂庫換成 CUE 曲目的身分（matchEmbeddedCue）。
    bool embeddedCue = false;
    /// 不查檔案系統就算找不到的原因（內嵌 cue 在音樂庫找不到）；空的＝照一般規則查。
    QString unmatched;
    /// 找不到的清單顯示用（.fpl 的「藝人 - 曲名」）；空的時顯示路徑。
    QString label;
};

/// 讀進來的播放清單檔。
struct ParsedPlaylist {
    /// 播放清單名稱：檔名去副檔名。
    QString name;
    QList<ImportEntry> entries;
    /// .fpl 的結構檢查沒過（AC-13-3「不支援此 .fpl 版本」）；這時 entries 是空的。
    bool unsupported = false;
};

/// 找不到的判斷（T-52 的重新對應與重新檢查也用這裡）：
/// - 被路徑規則拒絕的（網路路徑、裝置名稱、非本機 file://）算找不到，而且**不查詢檔案系統**；
/// - unmatched 有值的（內嵌 cue 在音樂庫找不到，T-53）也算找不到、不查詢；
/// - 其他的用 exists 查（磁碟上不存在＝找不到）。
/// 回傳每一筆是不是找不到（和 entries 同順序）。exists 會在呼叫端的執行緒被呼叫（會讀磁碟，
/// 不要在 UI 執行緒用）。
/// exists 的 start 是使用者親手給的起點（T-52 重新對應的「現在的資料夾」；起點本身不檢查，
/// 起點以下各層不跟隨符號連結與 junction，Architecture §8）；匯入時是空的（從磁碟機根目錄查）。
using ExistsCheck = std::function<bool(const QString& path, const QString& start)>;
QList<bool> findMissing(const QList<ImportEntry>& entries, const ExistsCheck& exists);

/// 匯入、重新對應與重新檢查共用的檔案查詢（T-52）：path 在磁碟上而且可以用。
/// 用核心 T-63 的 checkContentPath（start 以下各層不跟隨符號連結與 junction，起點本身不檢查；
/// 網路路徑、裝置名稱在查詢之前就拒絕）。工作執行緒。
bool contentPathExists(const QString& path, const QString& start);

/// 檔案內容寫的一個路徑 → 完整本機路徑（只收本機完整路徑；網路路徑、裝置名稱、控制字元一律拒絕，US-12、US-13）。
/// 只做字串處理，不查檔案系統：
/// - 絕對路徑（C:\…，/ 也可以）；相對路徑接在 baseDir（清單檔所在資料夾）後面；
///   \ 開頭的接在 baseDir 的磁碟機後面；
/// - file:/// URI：URL 解碼，只接受本機路徑（主機是空的或 localhost）；
/// - 拒絕：\\ 網路路徑（含 \\?\、\\.\，file://主機/…）、其他協定、裝置名稱（CON、NUL…）、
///   替代資料流、只有磁碟機代號的相對路徑（C:abc）、中間含控制字元（hasControlChar）。
/// 例外：清單檔本身在網路上（使用者自己選的）時，相對路徑解析後仍在同一個 \\主機\共用 裡可以。
/// 可以指到音樂資料夾以外的本機檔案（§1 情況 3）。任何執行緒。
Result<QString> resolveEntryPath(const QString& raw, const QString& baseDir);

/// 含控制字元（U+0000–U+001F、U+007F）：.fpl 字串表的路徑可以夾 CR、LF（M-04 審查）。任何執行緒。
bool hasControlChar(const QString& text);
/// 控制字元換成 U+FFFD：被拒絕的原文存進資料庫、寫進匯出檔的註解時用，不留下換行。任何執行緒。
QString replaceControlChars(QString text);

/// 播放、右鍵（接著播放、加到佇列）、拖出、側欄播放這四個出口要不要擋下這一項：missing 的一律擋
/// （安全底線）。被路徑規則拒絕的原文可能是 \\ 網路路徑；路徑字串合法、但經過符號連結或 junction
/// 而判成找不到的，核心開檔會跟隨連結（可能連到網路）。找不到的檔案本來就播不了，所以不分原因；
/// 「不在音樂庫」的項目 missing 是 false，不受影響。任何執行緒。
bool blockedFromOpening(bool missing);

/// 匯出時照原文寫、不查音樂庫的項目：匯入時被路徑規則拒絕、照原文存起來的（missing 而且原文
/// 過不了 resolveEntryPath）。任何執行緒。
bool storedVerbatim(const QString& path, bool missing);

/// 找不到的原因（給 S-09 顯示）裡要特別說明的：路徑經過符號連結或 junction 時回傳說明，
/// 其他（不存在、讀不到）回傳空字串。用核心的 checkContentPath，被字串規則拒絕的不查詢。
/// 工作執行緒（會查檔案系統）。
QString linkReason(const QString& path);
/// 找不到的原因的查詢（預設 linkReason）；測試的假檔案查詢不設它（不碰真的磁碟）。
using ReasonCheck = std::function<QString(const QString& path)>;

/// 匯入時找不到、還沒有原因的項目（不是被拒絕、不是內嵌 cue）用 reason 補上原因，寫在 rejected
/// （S-09 顯示；路徑仍在 path）。reason 空的時不做。工作執行緒。
void addMissingReasons(QList<ImportEntry>& entries, const QList<bool>& missing,
                       const ReasonCheck& reason);

/// .m3u／.m3u8：忽略空行與 # 開頭的行（包含只有 # 的第一行、#EXTM3U、#EXTINF）；
/// CRLF、LF 都接受。text 是解碼後的內容。任何執行緒。
QList<ImportEntry> parseM3u(const QString& text, const QString& baseDir);

/// .pls：File<n>= 依 n 排序（行的順序可以亂）；Title<n>、Length<n> 與其他鍵忽略
/// （playlist_items 沒有標題欄）。同一個 n 寫兩次時用第一次的。任何執行緒。
QList<ImportEntry> parsePls(const QString& text, const QString& baseDir);

/// 讀播放清單檔並解析（唯讀開啟，不修改，AC-12-4）。編碼：BOM（UTF-8、UTF-16）→
/// 嚴格 UTF-8 → uchardet（Big5、Shift-JIS…）→ 系統 ANSI（核心 TextDecoder，32 MB 上限）。
/// 副檔名不分大小寫：m3u、m3u8、pls；fpl 照 playlist_fpl.h（T-53，結構不符時 unsupported）；
/// 其他是錯誤。讀不到（不存在、被鎖住、太大）時回傳 Error。執行緒：工作執行緒（會讀檔）。
Result<ParsedPlaylist> readPlaylistFile(const QString& file);

} // namespace auralis::mod::pl
