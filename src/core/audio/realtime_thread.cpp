#include "audio/realtime_thread.h"

#include <windows.h>

#include <avrt.h>

namespace auralis::core {

void RealtimeThread::start(std::function<bool()> step) {
    stop();
    m_step = std::move(step);
    m_stop.store(false, std::memory_order_release);
    m_mmcss.store(false, std::memory_order_release);
    m_thread = std::thread([this] { run(); });
}

void RealtimeThread::stop() noexcept {
    if (!m_thread.joinable()) {
        return;
    }
    m_stop.store(true, std::memory_order_release);
    m_thread.join();
    m_nativeId.store(0, std::memory_order_release);
}

void RealtimeThread::run() {
    m_nativeId.store(GetCurrentThreadId(), std::memory_order_release);
    // 登記失敗（例：MMCSS 服務停用）仍然照常跑，只是優先權較低；由呼叫端決定要不要回報。
    DWORD taskIndex = 0;
    const HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    m_mmcss.store(task != nullptr, std::memory_order_release);
    while (!m_stop.load(std::memory_order_acquire)) {
        if (!m_step()) {
            break;
        }
    }
    if (task != nullptr) {
        AvRevertMmThreadCharacteristics(task);
    }
}

} // namespace auralis::core
