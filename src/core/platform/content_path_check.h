#pragma once

#include <auralis/content_path.h>

#include <QString>

#include <functional>

namespace auralis::core {

/// 照 Windows 開檔時的規則正規化路徑（分隔符、.、..、結尾的點與空白、CON 這類裝置名稱）。
/// GetFullPathNameW 只做字串處理，不碰檔案系統。失敗時空字串。執行緒：任何執行緒。
QString fullPathName(const QString& path);

/// checkContentPath 查檔案系統的那一半，.cue 的 FILE 檢查也用它：full 是已通過字串檢查、
/// 已正規化的完整路徑；start 是已正規化的起點（空＝磁碟機根目錄；full 不在它底下時也從根目錄）。
/// 從起點之後一層一層讀目錄項目，碰到連結就停（回傳 LinkInPath 與那個項目），某一層不存在時
/// NotFound；都通過時 Usable（path 是 full）。每次查詢之前呼叫 probe（可以是空的），它回傳 false
/// 時當成不存在、不去查。執行緒：工作執行緒。
ContentPathCheck walkContentPath(const QString& full, const QString& start,
                                 const std::function<bool(const QString&)>& probe);

/// 測試用：checkContentPath 每次要查詢檔案系統之前呼叫，參數是要查的路徑；回傳 false 時當成
/// 不存在、不去查。用來證明被拒的路徑與連結另一邊一次都沒有被查詢。
/// 正式程式不設定；只能在沒有其他執行緒呼叫 checkContentPath 時設定或清除（傳空的 std::function）。
void setContentPathProbeHookForTest(std::function<bool(const QString&)> hook);

} // namespace auralis::core
