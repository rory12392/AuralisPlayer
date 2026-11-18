#pragma once

#include <QString>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace auralis::core {

/// 監看一個資料夾（含子資料夾）的變動：ReadDirectoryChangesW，一個資料夾一條執行緒。
/// 只讀事件，不開、不改資料夾裡的任何檔案；資料夾的控制代碼共用讀寫刪除，不妨礙其他程式
/// 搬移或刪除檔案。
class FolderWatcher {
public:
    struct Change {
        /// 改名（含搬移）分成舊名與新名兩筆 Removed／Added。
        enum class Kind { Added, Removed, Modified };
        Kind kind = Kind::Modified;
        QString name; ///< 相對於監看的資料夾
    };
    /// 有變動時在監看執行緒呼叫，要自己轉到需要的執行緒。changes 是空的表示事件太多、
    /// 緩衝區溢位或監看中斷，不知道是哪些，要當成任何東西都可能變了。
    using Changed = std::function<void(const std::vector<Change>& changes)>;
    /// 開不了資料夾或監看中斷（資料夾被刪掉、磁碟離線）時在監看執行緒呼叫一次，之後不再通知。
    using Failed = std::function<void(const QString& error)>;

    /// path 是給 Win32 的路徑（可以帶 \\?\）。開資料夾在監看執行緒做，建構不會被慢的磁碟卡住。
    FolderWatcher(std::wstring path, Changed changed, Failed failed);
    /// 停止監看並等執行緒結束；回傳後不再呼叫任何回呼。
    ~FolderWatcher();

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    /// 已經開好資料夾、正在收事件（之後的變動一定會通知）。任何執行緒。
    bool isActive() const { return m_active; }

private:
    void run();

    std::wstring m_path;
    Changed m_changed;
    Failed m_failed;
    void* m_stopEvent = nullptr; ///< HANDLE
    std::atomic<bool> m_active{false};
    std::thread m_thread;
};

} // namespace auralis::core
