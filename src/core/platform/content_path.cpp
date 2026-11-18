#include "platform/content_path_check.h"

#include "library/file_enumerator.h"

#include <QDir>
#include <QRegularExpression>

#include <windows.h>

namespace auralis::core {

namespace {

std::function<bool(const QString&)>& contentProbeHook() {
    static std::function<bool(const QString&)> hook;
    return hook;
}

bool hasWildcard(const QString& path) {
    // 萬用字元不會出現在真的檔名裡；交給 FindFirstFile 會變成比對別的項目
    static const QString wildcards = QStringLiteral("*?<>\"|");
    for (const QChar c : path) {
        if (wildcards.contains(c)) {
            return true;
        }
    }
    return false;
}

bool hasControlChar(const QString& path) {
    // 控制字元不會出現在真的檔名裡；內嵌的 \0 會讓 FindFirstFileExW 截斷、查到別的項目
    for (const QChar c : path) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

} // namespace

QString fullPathName(const QString& path) {
    const std::wstring input = path.toStdWString();
    const DWORD needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return {};
    }
    std::wstring output(needed, L'\0');
    const DWORD written = GetFullPathNameW(input.c_str(), needed, output.data(), nullptr);
    if (written == 0 || written >= needed) {
        return {};
    }
    output.resize(written);
    return QString::fromStdWString(output);
}

ContentPathCheck walkContentPath(const QString& full, const QString& start,
                                 const std::function<bool(const QString&)>& probe) {
    QString base = full.left(3); // 磁碟機根目錄 C:\，本身不是目錄項目
    if (!start.isEmpty()) {
        const QString prefix =
            start.endsWith(QLatin1Char('\\')) ? start : start + QLatin1Char('\\');
        if (full.compare(start, Qt::CaseInsensitive) == 0 ||
            full.compare(prefix, Qt::CaseInsensitive) == 0) {
            return {ContentPathStatus::Usable, full}; // 使用者給的起點本身不檢查
        }
        if (full.startsWith(prefix, Qt::CaseInsensitive)) {
            base = prefix;
        }
    }
    const QStringList parts = full.mid(base.size()).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    QString current = base;
    current.chop(1);
    for (const QString& part : parts) {
        current += QLatin1Char('\\') + part;
        if ((probe && !probe(current)) || hasWildcard(part)) {
            return {ContentPathStatus::NotFound, full};
        }
        // 只讀上一層的目錄項目，不開啟、不跟隨這一層
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW(toLongPath(current).c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) {
            return {ContentPathStatus::NotFound, full};
        }
        FindClose(find);
        if (isLinkEntry(data.dwFileAttributes, data.dwReserved0)) {
            return {ContentPathStatus::LinkInPath, current};
        }
    }
    return {ContentPathStatus::Usable, full};
}

void setContentPathProbeHookForTest(std::function<bool(const QString&)> hook) {
    contentProbeHook() = std::move(hook);
}

} // namespace auralis::core

namespace auralis {

ContentPathCheck checkContentPath(const QString& path, const QString& trustedStart) {
    const auto reject = [&path](ContentPathStatus status) {
        return ContentPathCheck{status, path};
    };
    if (core::hasControlChar(path)) {
        return reject(ContentPathStatus::InvalidPath);
    }
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QLatin1String("\\\\?\\")) ||
        native.startsWith(QLatin1String("\\\\.\\")) || native.startsWith(QLatin1String("\\??\\"))) {
        return reject(ContentPathStatus::DevicePath);
    }
    if (native.startsWith(QLatin1String("\\\\"))) {
        return reject(ContentPathStatus::NetworkPath);
    }
    const bool driveAbsolute = native.size() >= 3 && native.at(0).isLetter() &&
                               native.at(1) == QLatin1Char(':') &&
                               native.at(2) == QLatin1Char('\\');
    if (!driveAbsolute) {
        // 兩個字元以上的「名稱:」是協定；單一字母加冒號是磁碟機代號（C:a 是那顆磁碟的目前資料夾）
        static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
        return reject(scheme.match(native).hasMatch() ? ContentPathStatus::ProtocolPrefix
                                                      : ContentPathStatus::InvalidPath);
    }
    // 磁碟機代號之後的冒號是替代資料流
    if (native.indexOf(QLatin1Char(':'), 2) >= 0 || core::hasWildcard(native)) {
        return reject(ContentPathStatus::InvalidPath);
    }
    // CON、NUL、COM1 這類保留名稱（加任何副檔名也一樣）開的是裝置，不是檔案；Windows 11 也把
    // 上標數字的 COM¹–³、LPT¹–³ 與 CONIN$、CONOUT$ 當成裝置
    static const QRegularExpression device(
        QStringLiteral("(^|\\\\)(CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³]|CONIN\\$|CONOUT\\$)[ ]*"
                       "(\\.[^\\\\]*)?($|\\\\)"),
        QRegularExpression::CaseInsensitiveOption);
    if (device.match(native).hasMatch()) {
        return reject(ContentPathStatus::DeviceName);
    }
    const QString full = core::fullPathName(native);
    // 正規化後也要是磁碟機開頭的本機路徑（GetFullPathNameW 會把裝置名稱換成 \\.\ 開頭）
    if (full.startsWith(QLatin1String("\\\\"))) {
        return reject(ContentPathStatus::DeviceName);
    }
    if (full.size() < 3 || full.at(1) != QLatin1Char(':')) {
        return reject(ContentPathStatus::InvalidPath);
    }
    const QString start = trustedStart.isEmpty()
                              ? QString()
                              : core::fullPathName(QDir::toNativeSeparators(trustedStart));
    ContentPathCheck result = core::walkContentPath(full, start, core::contentProbeHook());
    if (result.status == ContentPathStatus::NotFound) {
        result.path = path;
    }
    return result;
}

} // namespace auralis
