#include "plugins/plugin_loader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include <windows.h>

#include <array>

namespace auralis::core {

namespace {

/// C++ 例外在 Windows 上的結構化例外代碼（MSVC）。
constexpr DWORD kCppExceptionCode = 0xE06D7363;

/// 呼叫進入點：外掛丟出的 C++ 例外與存取違規都在這裡接住，不讓主程式結束。
/// __try 的函式裡不能有要解構的 C++ 物件，所以單獨一個函式。
const AuralisPluginEntry* callEntry(AuralisPluginEntryFn entry, DWORD* code) noexcept {
    __try {
        return entry();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return nullptr;
    }
}

/// 從已開的檔案算 SHA-256。
bool hashHandle(HANDLE file, QByteArray& out) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            return false;
        }
        if (read == 0) {
            break;
        }
        hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(read)));
    }
    out = hash.result().toHex();
    return true;
}

/// 只讀、只讓別人讀（不能寫、不能刪或改名）的方式開檔。
HANDLE openLocked(const QString& path) {
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    return CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

QString lastErrorText(DWORD error) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    QString message =
        length > 0 ? QString::fromWCharArray(text, static_cast<qsizetype>(length)) : QString();
    LocalFree(text);
    return QStringLiteral("%1（0x%2）")
        .arg(message.trimmed())
        .arg(static_cast<qulonglong>(error), 8, 16, QLatin1Char('0'));
}

} // namespace

Result<QByteArray> sha256OfFile(const QString& path) {
    const HANDLE file = openLocked(path);
    if (file == INVALID_HANDLE_VALUE) {
        return Error{QStringLiteral("讀不到 %1：%2")
                         .arg(QFileInfo(path).fileName(), lastErrorText(GetLastError())),
                     0, QStringLiteral("sha256OfFile")};
    }
    QByteArray hash;
    const bool ok = hashHandle(file, hash);
    CloseHandle(file);
    if (!ok) {
        return Error{QStringLiteral("讀不到 %1").arg(QFileInfo(path).fileName()), 0,
                     QStringLiteral("sha256OfFile")};
    }
    return hash;
}

PluginLoadResult loadPluginDll(const QString& dllPath, const QByteArray& expectedSha256) {
    PluginLoadResult result;
    const QString absolute = QDir::cleanPath(QFileInfo(dllPath).absoluteFilePath());
    // 開著檔案直到載入完成：比對過的內容就是載入的內容
    const HANDLE file = openLocked(absolute);
    if (file == INVALID_HANDLE_VALUE) {
        result.reason = QStringLiteral("讀不到外掛的 DLL：%1").arg(lastErrorText(GetLastError()));
        return result;
    }
    QByteArray actual;
    if (!hashHandle(file, actual)) {
        CloseHandle(file);
        result.reason = QStringLiteral("讀不到外掛的 DLL");
        return result;
    }
    if (expectedSha256.isEmpty() || actual.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
        CloseHandle(file);
        result.kind = PluginLoadResult::Kind::HashMismatch;
        result.reason = QStringLiteral("不是這個版本的官方外掛（檔案不符）");
        return result;
    }
    const std::wstring native = QDir::toNativeSeparators(absolute).toStdWString();
    const HMODULE module = LoadLibraryExW(
        native.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const DWORD loadError = GetLastError();
    CloseHandle(file);
    if (module == nullptr) {
        result.reason = QStringLiteral("無法載入外掛：%1").arg(lastErrorText(loadError));
        return result;
    }
    const auto entry =
        reinterpret_cast<AuralisPluginEntryFn>(GetProcAddress(module, AURALIS_PLUGIN_ENTRY_NAME));
    if (entry == nullptr) {
        FreeLibrary(module);
        result.reason =
            QStringLiteral("外掛沒有進入點 %1").arg(QLatin1String(AURALIS_PLUGIN_ENTRY_NAME));
        return result;
    }
    DWORD code = 0;
    const AuralisPluginEntry* info = callEntry(entry, &code);
    if (info == nullptr) {
        FreeLibrary(module);
        if (code == kCppExceptionCode) {
            result.reason = QStringLiteral("外掛初始化時丟出例外");
        } else if (code != 0) {
            result.reason = QStringLiteral("外掛初始化時發生錯誤（0x%1）")
                                .arg(static_cast<qulonglong>(code), 8, 16, QLatin1Char('0'));
        } else {
            result.reason = QStringLiteral("外掛初始化失敗");
        }
        return result;
    }
    if (info->abiVersion != AURALIS_PLUGIN_ABI_VERSION) {
        const auto version = info->abiVersion;
        FreeLibrary(module);
        result.kind = PluginLoadResult::Kind::AbiMismatch;
        result.reason = QStringLiteral("外掛介面版本 %1，這一版需要 %2，請更新外掛")
                            .arg(version)
                            .arg(AURALIS_PLUGIN_ABI_VERSION);
        return result;
    }
    result.kind = PluginLoadResult::Kind::Loaded;
    result.entry = info;
    return result;
}

} // namespace auralis::core
