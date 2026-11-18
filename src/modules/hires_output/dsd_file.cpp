#include "dsd_file.h"

#include <QDir>
#include <QFileInfo>

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace auralis::mod::out {

namespace {

/// 加上長路徑前綴：超過 260 字元的路徑不靠執行檔的設定也開得了。
std::wstring nativeLongPath(const QString& path) {
    if (path.startsWith(QStringLiteral(R"(\\?\)")) || path.startsWith(QStringLiteral(R"(\\.\)"))) {
        return path.toStdWString();
    }
    const QString native =
        QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (native.startsWith(QStringLiteral(R"(\\)"))) {
        return (QStringLiteral(R"(\\?\UNC\)") + native.mid(2)).toStdWString();
    }
    return (QStringLiteral(R"(\\?\)") + native).toStdWString();
}

Error openError(DWORD code) {
    Error error = hresultError(HRESULT_FROM_WIN32(code), "CreateFileW");
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        error.message = QStringLiteral("找不到檔案");
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        error.message = QStringLiteral("沒有讀取權限，或檔案正被其他程式使用");
        break;
    default:
        break;
    }
    return error;
}

} // namespace

Result<std::unique_ptr<DsdFile>> DsdFile::open(const QString& path) {
    // 共用讀寫刪除：播放中使用者照常可以搬移或刪除檔案
    const HANDLE handle = CreateFileW(nativeLongPath(path).c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return openError(GetLastError());
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size)) {
        const DWORD code = GetLastError();
        CloseHandle(handle);
        return hresultError(HRESULT_FROM_WIN32(code), "GetFileSizeEx");
    }
    return std::unique_ptr<DsdFile>(new DsdFile(handle, size.QuadPart));
}

DsdFile::~DsdFile() {
    CloseHandle(m_handle);
}

bool DsdFile::readAt(qint64 offset, void* dest, qint64 length) noexcept {
    if (offset < 0 || length < 0 || offset > m_size || length > m_size - offset) {
        return false;
    }
    auto* out = static_cast<char*>(dest);
    while (length > 0) {
        OVERLAPPED position{};
        position.Offset = static_cast<DWORD>(static_cast<quint64>(offset) & 0xFFFFFFFFu);
        position.OffsetHigh = static_cast<DWORD>(static_cast<quint64>(offset) >> 32);
        const DWORD want = static_cast<DWORD>(length < (1 << 20) ? length : (1 << 20));
        DWORD got = 0;
        // 讀不到東西代表檔案在開啟後被截短
        if (!ReadFile(m_handle, out, want, &got, &position) || got == 0) {
            return false;
        }
        out += got;
        offset += got;
        length -= got;
    }
    return true;
}

} // namespace auralis::mod::out
