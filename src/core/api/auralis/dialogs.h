#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace auralis {

/// 確認對話框的內容（介面規範：標題寫動作與對象、內文寫後果、確認鈕重複動作）。
/// 文字由呼叫端用 tr() 翻好；預設焦點在「取消」。
struct ConfirmRequest {
    QString title;
    QString message;
    /// 確認鈕的文字，例：「刪除播放清單」（不用「確定」）。
    QString confirmText;
    /// 破壞性操作時確認鈕用 signal 色。
    bool destructive = true;
};

/// 檔案選擇與確認對話框。都是非同步：結果以回呼送回 UI 執行緒。
/// 只在 UI 執行緒呼叫；沒有主視窗時（例：自我檢查）請求會一直等不到回應。
class Dialogs {
public:
    /// 選好的路徑；使用者取消時是空清單。
    using PathsCallback = std::function<void(const QStringList& paths)>;

    virtual ~Dialogs() = default;
    Dialogs(const Dialogs&) = delete;
    Dialogs& operator=(const Dialogs&) = delete;

    virtual void chooseFolder(const QString& title, PathsCallback done) = 0;
    /// nameFilters 例：{"播放清單 (*.m3u *.m3u8 *.pls *.fpl)"}。
    virtual void chooseFiles(const QString& title, const QStringList& nameFilters,
                             PathsCallback done) = 0;
    /// 另存新檔；回傳的清單最多一個路徑。已存在時系統對話框不問覆蓋，確認由呼叫端自己用
    /// confirm() 做（只問一次）。suggestedName 是完整路徑時，對話框從那個資料夾開始並預填檔名；
    /// 只有檔名時用系統記得的上一個資料夾。
    virtual void chooseSaveFile(const QString& title, const QString& suggestedName,
                                const QStringList& nameFilters, PathsCallback done) = 0;
    /// done(true) 表示使用者按了確認鈕。
    virtual void confirm(const ConfirmRequest& request, std::function<void(bool)> done) = 0;

protected:
    Dialogs() = default;
};

} // namespace auralis
