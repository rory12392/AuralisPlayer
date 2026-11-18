#include "platform/folder_watcher.h"

#include <auralis/result.h>

#include <windows.h>

#include <vector>

namespace auralis::core {

namespace {

/// 一次讀回的事件緩衝（本機磁碟上限 64 KB；不夠時系統回報溢位，照樣當成「有變動」）
constexpr DWORD kBufferBytes = 64 * 1024;

/// 新增、刪除、改名（含搬移）、檔案大小與修改時間。不收存取時間：掃描讀檔會改它，收了會自己觸發自己。
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

QString win32Error(DWORD error, const char* call) {
    return hresultError(HRESULT_FROM_WIN32(error), call).message;
}

/// 解讀 ReadDirectoryChangesW 交回的 FILE_NOTIFY_INFORMATION 串列。
std::vector<FolderWatcher::Change> parseChanges(const DWORD* buffer, DWORD bytes) {
    using Kind = FolderWatcher::Change::Kind;
    std::vector<FolderWatcher::Change> changes;
    const auto* entry = reinterpret_cast<const BYTE*>(buffer);
    const BYTE* const end = entry + bytes;
    while (bytes > 0 && entry < end) {
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(entry);
        FolderWatcher::Change change;
        switch (info->Action) {
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME:
            change.kind = Kind::Added;
            break;
        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME:
            change.kind = Kind::Removed;
            break;
        default:
            change.kind = Kind::Modified;
            break;
        }
        change.name = QString::fromWCharArray(
            info->FileName, static_cast<qsizetype>(info->FileNameLength / sizeof(wchar_t)));
        changes.push_back(std::move(change));
        if (info->NextEntryOffset == 0) {
            break;
        }
        entry += info->NextEntryOffset;
    }
    return changes;
}

} // namespace

FolderWatcher::FolderWatcher(std::wstring path, Changed changed, Failed failed)
    : m_path(std::move(path)), m_changed(std::move(changed)), m_failed(std::move(failed)) {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent == nullptr) {
        // 沒有停止事件就無法安全地結束執行緒，所以不開始監看
        m_failed(win32Error(GetLastError(), "CreateEventW"));
        return;
    }
    m_thread = std::thread([this] { run(); });
}

FolderWatcher::~FolderWatcher() {
    if (m_thread.joinable()) {
        SetEvent(m_stopEvent);
        m_thread.join();
    }
    if (m_stopEvent != nullptr) {
        CloseHandle(m_stopEvent);
    }
}

void FolderWatcher::run() {
    // 只要列出目錄的權限；共用讀寫刪除，監看中的資料夾照樣可以被搬移、刪除
    const HANDLE dir = CreateFileW(
        m_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        m_failed(win32Error(GetLastError(), "CreateFileW"));
        return;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        m_failed(win32Error(GetLastError(), "CreateEventW"));
        CloseHandle(dir);
        return;
    }
    // ReadDirectoryChangesW 要求 DWORD 對齊
    std::vector<DWORD> buffer(kBufferBytes / sizeof(DWORD));
    const HANDLE waits[] = {static_cast<HANDLE>(m_stopEvent), overlapped.hEvent};

    while (true) {
        ResetEvent(overlapped.hEvent);
        if (!ReadDirectoryChangesW(dir, buffer.data(), kBufferBytes, TRUE, kNotifyFilter, nullptr,
                                   &overlapped, nullptr)) {
            m_active = false;
            m_failed(win32Error(GetLastError(), "ReadDirectoryChangesW"));
            break;
        }
        m_active = true;
        const DWORD signalled = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        DWORD bytes = 0;
        if (signalled != WAIT_OBJECT_0 + 1) {
            // 停止：取消還在等的讀取，等它真的結束才能釋放緩衝
            CancelIoEx(dir, &overlapped);
            GetOverlappedResult(dir, &overlapped, &bytes, TRUE);
            break;
        }
        if (!GetOverlappedResult(dir, &overlapped, &bytes, FALSE)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NOTIFY_ENUM_DIR) {
                // 資料夾被刪掉或磁碟離線：通知一次變動讓掃描記下現況，再回報中斷
                m_active = false;
                m_changed({});
                m_failed(win32Error(error, "ReadDirectoryChangesW"));
                break;
            }
        }
        // bytes 為 0 或 ERROR_NOTIFY_ENUM_DIR 是事件太多、緩衝溢位：交出空清單＝不知道哪些變了
        m_changed(parseChanges(buffer.data(), bytes));
    }
    m_active = false;
    CloseHandle(overlapped.hEvent);
    CloseHandle(dir);
}

} // namespace auralis::core
