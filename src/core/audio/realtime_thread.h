#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace auralis::core {

/// 即時音訊執行緒：登記 MMCSS「Pro Audio」後反覆呼叫 step，直到 stop() 或 step 回傳 false。
/// 輸出後端（Capture、WASAPI）用它跑自己的音訊執行緒；step 裡遵守即時路徑規則。
/// 執行緒：start()、stop() 在非即時執行緒呼叫；同一個物件不能同時 start 兩次。
class RealtimeThread {
public:
    RealtimeThread() = default;
    ~RealtimeThread() { stop(); }
    RealtimeThread(const RealtimeThread&) = delete;
    RealtimeThread& operator=(const RealtimeThread&) = delete;

    void start(std::function<bool()> step);
    /// 要求結束並等執行緒結束；沒有在跑時什麼都不做。
    void stop() noexcept;

    /// 執行緒是否成功登記 MMCSS（執行緒開始後才有意義）。
    bool mmcssRegistered() const noexcept { return m_mmcss.load(std::memory_order_acquire); }
    /// 執行緒的 Windows 執行緒 ID；沒有在跑時為 0。
    unsigned long nativeId() const noexcept { return m_nativeId.load(std::memory_order_acquire); }
    /// 給 step 用：是否被要求結束。
    bool stopRequested() const noexcept { return m_stop.load(std::memory_order_acquire); }

private:
    void run();

    std::function<bool()> m_step;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_mmcss{false};
    std::atomic<unsigned long> m_nativeId{0};
};

} // namespace auralis::core
