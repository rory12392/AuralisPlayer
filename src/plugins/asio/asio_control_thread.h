// ASIO 驅動的控制執行緒：載入、init、開關緩衝、start／stop 都在這裡做。
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace auralis::mod::out {

/// ASIO 驅動是單執行緒套間（STA）的 COM 物件，建立它的執行緒要初始化 COM，之後的呼叫也要在
/// 同一條執行緒；後端的 open 與 close 卻可能分別在 UI 執行緒與解碼執行緒呼叫，所以後端自己
/// 開一條 STA 執行緒，所有驅動呼叫都交給它（等它做完才回傳）。等待時處理視窗訊息，
/// 驅動用訊息溝通時不會卡住。不是即時路徑：音訊回呼在驅動自己的執行緒。
class AsioControlThread {
public:
    AsioControlThread();
    ~AsioControlThread();
    AsioControlThread(const AsioControlThread&) = delete;
    AsioControlThread& operator=(const AsioControlThread&) = delete;

    /// 在控制執行緒執行 task 並等它做完。task 不能丟例外。任何非即時執行緒；同時呼叫時依序執行。
    void run(const std::function<void()>& task);

private:
    void loop();

    std::mutex m_callMutex; // 一次只交一件事
    std::mutex m_mutex;
    std::condition_variable m_done;
    void* m_wake = nullptr; // 事件 HANDLE（標頭不引入 windows.h）
    const std::function<void()>* m_task = nullptr;
    bool m_finished = false;
    bool m_quit = false;
    std::thread m_thread;
};

} // namespace auralis::mod::out
