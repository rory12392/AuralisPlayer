#include "library/read_only_stream.h"

#include "library/file_enumerator.h"

#include <auralis/result.h>

#include <QDir>
#include <QFileInfo>

#include <algorithm>

#include <windows.h>

namespace auralis::core {

ReadOnlyStream::ReadOnlyStream(const QString& path)
    // \\?\ 長路徑：超過 260 字元的路徑不靠執行檔的 longPathAware 設定也開得了
    : m_path(toLongPath(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()))),
      m_handle(INVALID_HANDLE_VALUE) {
    // 共用讀寫刪除：掃描中使用者照常可以播放、搬移或刪除檔案
    m_handle = CreateFileW(m_path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_openError = hresultError(HRESULT_FROM_WIN32(GetLastError()), "CreateFileW").message;
    }
}

ReadOnlyStream::~ReadOnlyStream() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
    }
}

TagLib::FileName ReadOnlyStream::name() const {
    return m_path.c_str();
}

TagLib::ByteVector ReadOnlyStream::readBlock(size_t length) {
    if (!isOpen() || length == 0) {
        return {};
    }
    // 不依檔頭宣告的長度配置記憶體：最多讀到檔尾（損毀或惡意的檔頭可能宣告數 GB 的區塊）
    const TagLib::offset_t remaining = this->length() - tell();
    if (remaining <= 0) {
        return {};
    }
    length = std::min(length, static_cast<size_t>(remaining));
    if (length > kMaxReadBlock) {
        m_readLimitExceeded = true;
        return {};
    }
    TagLib::ByteVector buffer(static_cast<unsigned int>(length), 0);
    DWORD read = 0;
    if (!ReadFile(m_handle, buffer.data(), static_cast<DWORD>(length), &read, nullptr)) {
        return {};
    }
    buffer.resize(static_cast<unsigned int>(read));
    return buffer;
}

// 寫入類的方法：唯讀串流，一律不做事（readOnly() 為 true，TagLib 不會在掃描時呼叫）
void ReadOnlyStream::writeBlock(const TagLib::ByteVector&) {}
void ReadOnlyStream::insert(const TagLib::ByteVector&, TagLib::offset_t, size_t) {}
void ReadOnlyStream::removeBlock(TagLib::offset_t, size_t) {}
void ReadOnlyStream::truncate(TagLib::offset_t) {}

bool ReadOnlyStream::isOpen() const {
    return m_handle != INVALID_HANDLE_VALUE;
}

void ReadOnlyStream::seek(TagLib::offset_t offset, Position p) {
    if (!isOpen()) {
        return;
    }
    DWORD method = FILE_BEGIN;
    if (p == Current) {
        method = FILE_CURRENT;
    } else if (p == End) {
        method = FILE_END;
    }
    LARGE_INTEGER distance;
    distance.QuadPart = offset;
    SetFilePointerEx(m_handle, distance, nullptr, method);
}

TagLib::offset_t ReadOnlyStream::tell() const {
    LARGE_INTEGER zero{};
    LARGE_INTEGER position{};
    if (!isOpen() || !SetFilePointerEx(m_handle, zero, &position, FILE_CURRENT)) {
        return 0;
    }
    return position.QuadPart;
}

TagLib::offset_t ReadOnlyStream::length() {
    LARGE_INTEGER size{};
    if (!isOpen() || !GetFileSizeEx(m_handle, &size)) {
        return 0;
    }
    return size.QuadPart;
}

} // namespace auralis::core
