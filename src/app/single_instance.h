#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace auralis::app {

/// 單一執行個體（AC-11-5）：同一個資料夾（設定與資料庫）只開一個主視窗。程式已在執行時再用
/// `AuralisPlayer.exe <路徑…>` 啟動，新的行程把路徑交給已開的視窗後就結束。
/// 做法：以資料夾算出名稱的具名管道（只收本機連線）；第一個建立的行程是主要的執行個體。
/// 傳的只是路徑字串，主要的執行個體照樣經過路徑檢查（和命令列、拖放同一套）。
class SingleInstance {
public:
    /// key 是資料夾的完整路徑；同一個資料夾（不分大小寫與分隔符）得到同一個管道。
    explicit SingleInstance(const QString& key);
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    /// 這個行程搶到了管道（沒有別的執行個體）。管道建立因其他原因失敗時也是 true（照常自己開）。
    bool isPrimary() const { return m_primary; }

    /// 次要的執行個體：把完整路徑交給主要的執行個體（空清單＝只把視窗叫到前面），回傳是否送達。
    /// 主要的執行個體忙或剛啟動時最多等 kConnectTimeoutMs。
    bool forward(const QStringList& paths) const;

    /// 主要的執行個體：開始在背景接收；收到時在 receiver 的執行緒呼叫 handler。只呼叫一次；
    /// receiver 拆掉之前要先 stop()。
    void listen(QObject* receiver, std::function<void(const QStringList&)> handler);
    /// 停止接收並等背景執行緒結束（管道留著，別的行程仍然當這裡是主要的執行個體）。可以重複呼叫。
    void stop();

    static constexpr int kConnectTimeoutMs = 5000;
    /// 一則訊息的上限；超過的整則丟掉。
    static constexpr qsizetype kMaxMessageBytes = 1 << 20;

private:
    void serve();

    std::wstring m_name;
    void* m_pipe = nullptr; // HANDLE；不在標頭 include windows.h
    bool m_primary = true;
    std::atomic<bool> m_stopping{false};
    QObject* m_receiver = nullptr;
    std::function<void(const QStringList&)> m_handler;
    std::thread m_thread;
};

} // namespace auralis::app
