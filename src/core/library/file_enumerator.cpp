#include "library/file_enumerator.h"

#include "covers/cover_key.h"
#include "library/tag_reader.h"

#include <auralis/result.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <windows.h>

namespace auralis::core {

namespace {

/// FILETIME（1601 年起的 100 奈秒）轉 Unix 奈秒。
qint64 toUnixNs(const FILETIME& time) {
    constexpr qint64 kEpochDiff100ns = 116444736000000000LL;
    const qint64 ticks =
        (static_cast<qint64>(time.dwHighDateTime) << 32) | static_cast<qint64>(time.dwLowDateTime);
    return (ticks - kEpochDiff100ns) * 100;
}

QString suffixOf(const QString& name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

QString joinPath(const QString& folder, const QString& name) {
    return folder.endsWith(QLatin1Char('\\')) ? folder + name : folder + QLatin1Char('\\') + name;
}

} // namespace

std::wstring toLongPath(const QString& path) {
    if (path.startsWith(QLatin1String("\\\\?\\"))) {
        return path.toStdWString();
    }
    if (path.startsWith(QLatin1String("\\\\"))) {
        return (QStringLiteral("\\\\?\\UNC\\") + path.mid(2)).toStdWString();
    }
    return (QStringLiteral("\\\\?\\") + path).toStdWString();
}

QString cleanFolderPath(const QString& path) {
    QString clean = QDir::toNativeSeparators(QDir::cleanPath(path));
    while (clean.size() > 3 && clean.endsWith(QLatin1Char('\\'))) {
        clean.chop(1);
    }
    return clean;
}

bool isLinkEntry(unsigned long attributes, unsigned long reparseTag) {
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           (reparseTag == IO_REPARSE_TAG_SYMLINK || reparseTag == IO_REPARSE_TAG_MOUNT_POINT);
}

QString linkSkippedReason() {
    return QStringLiteral("符號連結，已跳過");
}

bool isLinkPath(const QString& path) {
    // 萬用字元不會出現在真的檔名裡；交給 FindFirstFile 會變成比對別的項目
    static const QString wildcards = QStringLiteral("*?<>\"");
    for (const QChar c : path.mid(4)) {
        if (wildcards.contains(c)) {
            return false;
        }
    }
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(toLongPath(path).c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    FindClose(find);
    return isLinkEntry(data.dwFileAttributes, data.dwReserved0);
}

Enumeration enumerateAudioFiles(const QString& root, const std::function<bool()>& cancelled) {
    // 音樂檔加上 .cue（整軌檔依 .cue 分軌，AC-07-1）
    static const QSet<QString> extensions = [] {
        QSet<QString> set(audioExtensions().cbegin(), audioExtensions().cend());
        set.insert(QStringLiteral("cue"));
        return set;
    }();
    Enumeration result;
    const QString rootPath = cleanFolderPath(root);
    result.folders.push_back({rootPath, QDir(rootPath).dirName(), -1});

    // 深度優先、先序：stack 放還沒列舉的資料夾索引；子資料夾反向推入，讓順序和名稱順序一致
    std::vector<int> stack{0};
    while (!stack.empty()) {
        if (cancelled()) {
            result.cancelled = true;
            return result;
        }
        const int index = stack.back();
        stack.pop_back();
        const QString folderPath = result.folders[static_cast<size_t>(index)].path;

        WIN32_FIND_DATAW data;
        const std::wstring pattern = toLongPath(joinPath(folderPath, QStringLiteral("*")));
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND) {
                result.unreadableFolders.append(
                    {folderPath,
                     hresultError(HRESULT_FROM_WIN32(code), "FindFirstFileExW").message});
            }
            continue;
        }
        std::vector<int> children;
        do {
            const QString name = QString::fromWCharArray(data.cFileName);
            if (name == QLatin1String(".") || name == QLatin1String("..")) {
                continue;
            }
            const QString path = joinPath(folderPath, name);
            const bool link = isLinkEntry(data.dwFileAttributes, data.dwReserved0);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    children.push_back(static_cast<int>(result.folders.size()));
                    result.folders.push_back({path, name, index});
                } else if (link) {
                    result.skippedLinks.append({path, linkSkippedReason()});
                }
                continue;
            }
            if (link) {
                // 檔案層級的符號連結：只有本來會收的（音樂檔、.cue、封面圖檔）才列進報告
                if (folderCoverRank(name) >= 0 || extensions.contains(suffixOf(name))) {
                    result.skippedLinks.append({path, linkSkippedReason()});
                }
                continue;
            }
            const qint64 size = (static_cast<qint64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            if (const int rank = folderCoverRank(name); rank >= 0) {
                EnumeratedFolder& folder = result.folders[static_cast<size_t>(index)];
                if (folder.coverImage.isEmpty() ||
                    rank < folderCoverRank(QFileInfo(folder.coverImage).fileName())) {
                    folder.coverImage = path;
                    folder.coverSize = size;
                    folder.coverMtimeNs = toUnixNs(data.ftLastWriteTime);
                }
                continue;
            }
            if (!extensions.contains(suffixOf(name))) {
                continue;
            }
            result.files.push_back({path, index, size, toUnixNs(data.ftLastWriteTime)});
        } while (FindNextFileW(find, &data));
        FindClose(find);
        stack.insert(stack.end(), children.rbegin(), children.rend());
    }
    return result;
}

} // namespace auralis::core
