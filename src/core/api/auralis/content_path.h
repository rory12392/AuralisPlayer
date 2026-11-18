#pragma once

#include <QString>

namespace auralis {

/// checkContentPath 的結果。除了 Usable 都是拒絕，原因給日誌與錯誤訊息用。
enum class ContentPathStatus {
    Usable,      ///< 可以開
    InvalidPath, ///< 不是磁碟機開頭的絕對路徑（相對路徑、C:a）、含 * ? < > " |、替代資料流、
                 ///< 含控制字元（U+0000–U+001F、U+007F）
    NetworkPath, ///< \\server\share（UNC）
    DevicePath,  ///< \\?\、\\.\、\??\ 開頭
    DeviceName, ///< 某一層是 CON、PRN、AUX、NUL、COM0–9、LPT0–9、COM¹–³、LPT¹–³、CONIN$、
                ///< CONOUT$（加副檔名也算）
    ProtocolPrefix, ///< 開頭是「名稱:」而且不是磁碟機代號，例 http:、file:
    LinkInPath,     ///< 起點之後某一層（含最後一層）是符號連結或 junction
    NotFound,       ///< 某一層不存在或讀不到
};

struct ContentPathCheck {
    ContentPathStatus status = ContentPathStatus::NotFound;
    /// Usable：照 Windows 開檔規則正規化後的完整路徑（Windows 分隔符），之後開檔用這個；
    /// LinkInPath：那個連結項目本身；其他：原本的輸入。
    QString path;

    bool usable() const noexcept { return status == ContentPathStatus::Usable; }
};

/// 檢查「檔案內容寫的路徑」（播放清單的項目、.cue 的 FILE 這類）能不能開，不跟隨符號連結與
/// junction：連結可以指到網路位置，查它另一邊會對外連 SMB 並送出使用者的 NTLM 驗證，而本程式的
/// 對外連線只有歌詞服務。
/// path：已轉成絕對路徑的本機路徑。trustedStart：使用者親手給的起點（音樂資料夾、拖進來或選的
/// 那個路徑）；空字串＝從磁碟機根目錄開始，path 不在起點底下時也從磁碟機根目錄開始。
/// 先做字串檢查（網路路徑、裝置路徑、裝置名稱、協定前綴），被拒的路徑一次都不會查詢檔案系統；
/// 通過後從起點往下一層一層讀目錄項目（不開啟、不跟隨），碰到連結就停，不查連結另一邊。
/// 起點本身不檢查（使用者自己給的）。雲端硬碟佔位檔等不是連結的重新解析點照常可用。
/// 支援超過 260 字元的路徑。
/// 執行緒：會查檔案系統，在工作執行緒呼叫；可以同時從多個執行緒呼叫。
ContentPathCheck checkContentPath(const QString& path, const QString& trustedStart = {});

} // namespace auralis
