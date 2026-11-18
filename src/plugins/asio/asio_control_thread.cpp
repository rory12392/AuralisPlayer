#include "asio_control_thread.h"

#include <windows.h>

#include <objbase.h>

namespace auralis::mod::out {

AsioControlThread::AsioControlThread() {
    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_thread = std::thread([this] { loop(); });
}

AsioControlThread::~AsioControlThread() {
    {
        const std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    SetEvent(m_wake);
    m_thread.join();
    CloseHandle(m_wake);
}

void AsioControlThread::run(const std::function<void()>& task) {
    const std::lock_guard call(m_callMutex);
    std::unique_lock lock(m_mutex);
    m_task = &task;
    m_finished = false;
    SetEvent(m_wake);
    m_done.wait(lock, [this] { return m_finished; });
    m_task = nullptr;
}

void AsioControlThread::loop() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        const DWORD waited = MsgWaitForMultipleObjects(1, &m_wake, FALSE, INFINITE, QS_ALLINPUT);
        if (waited == WAIT_OBJECT_0 + 1) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        std::unique_lock lock(m_mutex);
        if (m_task != nullptr && !m_finished) {
            (*m_task)();
            m_finished = true;
            m_done.notify_all();
        }
        if (m_quit) {
            break;
        }
    }
    if (SUCCEEDED(co)) {
        CoUninitialize();
    }
}

} // namespace auralis::mod::out
