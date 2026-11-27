// .fpl 的唯讀讀檔（解析在 playlist_fpl.cpp）。
#include "playlist_fpl.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <memory>

#include <windows.h>

namespace auralis::mod::pl {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

/// 長路徑前綴（\\?\、\\?\UNC\）；path 是完整的 Windows 路徑。
std::wstring longPath(const QString& path) {
    QString full = path;
    if (full.startsWith(QLatin1String("\\\\?\\"))) {
        // 已經是長路徑
    } else if (full.startsWith(QLatin1String("\\\\"))) {
        full = QLatin1String("\\\\?\\UNC\\") + full.mid(2);
    } else {
        full = QLatin1String("\\\\?\\") + full;
    }
    return full.toStdWString();
}

} // namespace

Result<QByteArray> readFplFile(const QString& path) {
    // 唯讀、共用讀寫刪除：使用者的播放清單一律不改（AC-12-4）
    const std::wstring name =
        longPath(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    HANDLE file = CreateFileW(name.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Error{tr("無法開啟檔案"), HRESULT_FROM_WIN32(GetLastError()),
                     QStringLiteral("CreateFileW")};
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(file, &CloseHandle);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return Error{tr("無法讀取檔案"), HRESULT_FROM_WIN32(GetLastError()),
                     QStringLiteral("GetFileSizeEx")};
    }
    // 超過上限＝讀不到（Architecture §8），不整個讀進記憶體
    if (size.QuadPart > kMaxFplBytes) {
        return Error{tr("檔案太大（%1 位元組）").arg(size.QuadPart), 0,
                     QStringLiteral("readFplFile")};
    }
    QByteArray bytes(static_cast<qsizetype>(size.QuadPart), Qt::Uninitialized);
    qsizetype done = 0;
    while (done < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &read,
                      nullptr)) {
            return Error{tr("無法讀取檔案"), HRESULT_FROM_WIN32(GetLastError()),
                         QStringLiteral("ReadFile")};
        }
        if (read == 0) {
            break; // 讀的期間檔案變短
        }
        done += read;
    }
    bytes.truncate(done);
    return bytes;
}

} // namespace auralis::mod::pl
