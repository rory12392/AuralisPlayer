#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

class QTimer;

namespace auralis::core {

class FolderWatcher;
class LibraryManager;

/// 音樂資料夾的監看（AC-08-3）：每個音樂資料夾一個 FolderWatcher，事件去抖 2 秒後
/// 交給 LibraryManager::rescan(那個音樂資料夾) 做增量掃描。跟著音樂資料夾清單增減。
/// 執行緒：只在 UI 執行緒使用（需要事件迴圈）；監看執行緒的通知以 queued 呼叫送回來。
class LibraryWatcher : public QObject {
    Q_OBJECT

public:
    /// 最後一個事件之後等這麼久才掃描（複製整張專輯時只掃一次）。
    static constexpr int kDebounceMs = 2000;

    explicit LibraryWatcher(LibraryManager& manager, QObject* parent = nullptr);
    /// 停止全部監看並等監看執行緒結束。
    ~LibraryWatcher() override;

    /// 程式啟動時呼叫一次：開始監看目前的音樂資料夾，並排一次全部的增量檢查
    /// （AC-08-2 重新啟動後的檢查，在背景的掃描工作裡做，不擋啟動）。之後跟著資料夾清單增減。
    /// 先開始監看再檢查，兩者之間的變動不會漏掉。
    void start();

    /// 已經開好、正在收事件的音樂資料夾（測試用）。
    QStringList activeFolders() const;

private:
    struct Watch;

    void syncFolders();
    void onChanged(const QString& key);

    LibraryManager& m_manager;
    bool m_started = false;
    /// pathKey → 監看與去抖計時器
    std::map<QString, std::unique_ptr<Watch>> m_watches;
};

} // namespace auralis::core
