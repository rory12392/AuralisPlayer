#include <auralis/result.h>

#include <windows.h>

namespace auralis {

Error hresultError(long hresult, const char* call) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hresult), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    // WASAPI 的 AUDCLNT_E_* 不在系統訊息表裡，那時只留十六進位碼，對照由呼叫端或日誌讀者處理
    QString message =
        QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(hresult), 8, 16, QLatin1Char('0'));
    if (length > 0 && text != nullptr) {
        message += QStringLiteral("：") +
                   QString::fromWCharArray(text, static_cast<qsizetype>(length)).trimmed();
    }
    if (text != nullptr) {
        LocalFree(text);
    }
    return Error{message, hresult, QString::fromUtf8(call)};
}

} // namespace auralis
