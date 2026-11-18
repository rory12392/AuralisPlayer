#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace auralis {

/// 模組送出的背景工作和「模組停止」之間的閘門（P-LYR-10、P-PL-24）：工作開始時先 enter()，
/// 關閉之後才開始的工作直接結束、不碰任何核心服務；close() 等已經開始的工作結束。
/// 核心的 JobRunner 最後才解構，音樂庫、資料庫、封面服務與模組自己的物件比它早，
/// 所以模組的 stop() 要先取消自己的工作、再 close()，不能只取消。
/// 執行緒：enter()／leave() 在工作執行緒，close() 在 UI 執行緒（模組的 stop()）；
/// 工作與模組各持有一份 shared_ptr，閘門活得比最後一個工作久。
class JobGate {
public:
    /// 工作開始時呼叫；回傳 false 時工作立刻 return（已經關閉）。true 時結束前一定要 leave()。
    /// 任何執行緒。
    bool enter() {
        std::lock_guard lock(m_mutex);
        if (m_closed) {
            return false;
        }
        ++m_running;
        return true;
    }

    /// 和成功的 enter() 成對呼叫。任何執行緒。
    void leave() {
        {
            std::lock_guard lock(m_mutex);
            --m_running;
        }
        m_idle.notify_all();
    }

    /// 關閉閘門並等已經開始的工作結束，最多 timeoutMs 毫秒；回傳是否都結束了。
    /// 逾時時呼叫端寫日誌後照常停止（不當機、不無限等）。呼叫前先取消工作，這裡不處理事件。
    /// 只在 UI 執行緒（模組的 stop()）呼叫；可以重複呼叫。
    bool close(int timeoutMs) noexcept {
        std::unique_lock lock(m_mutex);
        m_closed = true;
        return m_idle.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                               [this] { return m_running == 0; });
    }

    /// 工作函式開頭用：`JobGate::Pass pass(*gate); if (!pass) return;`，不論怎麼結束都會 leave。
    class Pass {
    public:
        explicit Pass(JobGate& gate) : m_gate(gate), m_entered(gate.enter()) {}
        ~Pass() {
            if (m_entered) {
                m_gate.leave();
            }
        }
        Pass(const Pass&) = delete;
        Pass& operator=(const Pass&) = delete;
        explicit operator bool() const { return m_entered; }

    private:
        JobGate& m_gate;
        bool m_entered;
    };

private:
    std::mutex m_mutex;
    std::condition_variable m_idle;
    int m_running = 0;
    bool m_closed = false;
};

} // namespace auralis
