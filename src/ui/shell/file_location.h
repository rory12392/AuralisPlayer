#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace auralis::ui {

/// 曲目右鍵選單的「開啟檔案位置」：在檔案總管開啟檔案所在的資料夾並選取該檔。
/// 不開啟檔案、不改任何檔案。只在 UI 執行緒使用
/// （檔案總管的 Shell API 要 COM，Qt 的 UI 執行緒已初始化）；檢查檔案存在在背景執行緒做，
/// 離線的網路磁碟、睡眠中的外接硬碟、雲端佔位檔可能讓檢查等很久，不能卡住介面。
// 由 QML 引擎建立（QML_SINGLETON），型別註冊會繼承它，所以不能標 final
class FileLocation : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using RevealHook = std::function<bool(const QString& path)>;
    using ExistsHook = std::function<bool(const QString& path)>;

    explicit FileLocation(QObject* parent = nullptr);

    /// path 是存在的檔案（不是資料夾）。會碰檔案系統，不要在 UI 執行緒呼叫。
    bool exists(const QString& path) const;
    /// 在背景執行緒確認 path 存在，再回到 UI 執行緒開檔案總管並選取它；檔案不在時發出錯誤通知
    /// 「找不到檔案：<檔名>」，開不了時寫日誌。馬上返回，結果用 revealFinished 通知。
    Q_INVOKABLE void reveal(const QString& path);

signals:
    /// reveal 結束（UI 執行緒）：shown 是檔案總管有沒有開。
    void revealFinished(const QString& path, bool shown);

public:
    /// 測試用：換掉開檔案總管的動作（空的 function 恢復預設）。
    static void setRevealHook(RevealHook hook);
    /// 測試用：換掉「檔案存在嗎」的檢查（空的 function 恢復預設）。可能在背景執行緒被呼叫。
    static void setExistsHook(ExistsHook hook);

private:
    void finishReveal(const QString& path, bool found);
};

} // namespace auralis::ui
