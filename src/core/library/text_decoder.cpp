#include <auralis/text_decoder.h>

#include "library/file_enumerator.h"

#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>

#include <uchardet/uchardet.h>

#include <windows.h>

#include <memory>
#include <optional>

namespace auralis {

namespace {

/// 嚴格解碼：有任何不合法的位元組就回傳 nullopt，交給下一個候選編碼。
std::optional<QString> decodeStrict(QStringConverter::Encoding encoding, QByteArrayView bytes) {
    QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) {
        return std::nullopt;
    }
    return text;
}

/// 用 Windows 字碼頁解碼。strict 時遇到不合法的位元組回傳 nullopt；
/// 有些字碼頁不支援 MB_ERR_INVALID_CHARS（例：50220），這時改成不檢查。
std::optional<QString> decodeCodePage(UINT codePage, QByteArrayView bytes, bool strict) {
    if (bytes.isEmpty()) {
        return QString();
    }
    const auto size = static_cast<int>(bytes.size());
    DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    int length = MultiByteToWideChar(codePage, flags, bytes.data(), size, nullptr, 0);
    if (length == 0 && strict && GetLastError() == ERROR_INVALID_FLAGS) {
        flags = 0;
        length = MultiByteToWideChar(codePage, flags, bytes.data(), size, nullptr, 0);
    }
    if (length == 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(codePage, flags, bytes.data(), size, wide.data(), length);
    return QString::fromStdWString(wide);
}

/// uchardet 的編碼名稱 → Windows 字碼頁；不認得時 0。
UINT codePageFor(const QByteArray& charset) {
    const QByteArray name = charset.toUpper();
    if (name == "SHIFT_JIS") {
        return 932;
    }
    if (name == "BIG5") {
        return 950;
    }
    if (name == "EUC-JP") {
        return 20932;
    }
    if (name == "EUC-KR" || name == "UHC") {
        return 949;
    }
    if (name == "GB18030") {
        return 54936;
    }
    if (name == "GB2312" || name == "HZ-GB-2312") {
        return 936;
    }
    if (name == "ISO-2022-JP") {
        return 50220;
    }
    if (name == "KOI8-R") {
        return 20866;
    }
    if (name == "TIS-620") {
        return 874;
    }
    bool ok = false;
    for (const char* prefix : {"WINDOWS-", "CP", "IBM"}) {
        if (name.startsWith(prefix)) {
            const UINT page = name.mid(static_cast<qsizetype>(qstrlen(prefix))).toUInt(&ok);
            return ok ? page : 0;
        }
    }
    if (name.startsWith("ISO-8859-")) {
        const UINT part = name.mid(9).toUInt(&ok);
        return ok ? 28590 + part : 0;
    }
    return 0;
}

/// uchardet 偵測編碼；偵測不到時回傳空字串。
QByteArray detectCharset(const QByteArray& bytes) {
    const std::unique_ptr<std::remove_pointer_t<uchardet_t>, decltype(&uchardet_delete)> detector(
        uchardet_new(), &uchardet_delete);
    if (!detector || uchardet_handle_data(detector.get(), bytes.constData(),
                                          static_cast<size_t>(bytes.size())) != 0) {
        return {};
    }
    uchardet_data_end(detector.get());
    return QByteArray(uchardet_get_charset(detector.get()));
}

} // namespace

DecodedText TextDecoder::decode(const QByteArray& bytes) {
    // 1. BOM
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        const auto body = QByteArrayView(bytes).sliced(3);
        return {decodeStrict(QStringConverter::Utf8, body).value_or(QString::fromUtf8(body)),
                QStringLiteral("UTF-8"), true};
    }
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        return {decoder.decode(QByteArrayView(bytes).sliced(2)), QStringLiteral("UTF-16LE"), true};
    }
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringConverter::Utf16BE);
        return {decoder.decode(QByteArrayView(bytes).sliced(2)), QStringLiteral("UTF-16BE"), true};
    }
    // 2. 嚴格 UTF-8（純 ASCII 也在這裡）
    if (auto text = decodeStrict(QStringConverter::Utf8, bytes)) {
        return {*text, QStringLiteral("UTF-8"), false};
    }
    // 3. uchardet 判斷的編碼，要能嚴格解碼才採用
    const QByteArray charset = detectCharset(bytes);
    if (const UINT page = codePageFor(charset); page != 0) {
        if (auto text = decodeCodePage(page, bytes, true)) {
            return {*text, QString::fromLatin1(charset), false};
        }
    }
    // 4. 系統 ANSI 字碼頁（一定解得出來；不合法的位元組變成替代字元）
    return {decodeCodePage(CP_ACP, bytes, false).value_or(QString()),
            QStringLiteral("CP%1").arg(GetACP()), false};
}

Result<DecodedText> TextDecoder::decodeFile(const QString& path) {
    // 唯讀、共用讀寫刪除：使用者的 .cue、.lrc、播放清單一律不改（AC-08-4）
    // \\?\ 長路徑不做正規化，所以先轉成絕對路徑與 Windows 分隔符（呼叫端可能給 Qt 風格的 /）
    const std::wstring longPath =
        core::toLongPath(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    HANDLE file = CreateFileW(longPath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return hresultError(HRESULT_FROM_WIN32(GetLastError()), "CreateFileW");
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(file, &CloseHandle);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return hresultError(HRESULT_FROM_WIN32(GetLastError()), "GetFileSizeEx");
    }
    if (size.QuadPart > kMaxFileBytes) {
        return Error{QStringLiteral("文字檔太大（%1 位元組）").arg(size.QuadPart), 0,
                     QStringLiteral("TextDecoder::decodeFile")};
    }
    QByteArray bytes(static_cast<qsizetype>(size.QuadPart), Qt::Uninitialized);
    qsizetype done = 0;
    while (done < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &read,
                      nullptr)) {
            return hresultError(HRESULT_FROM_WIN32(GetLastError()), "ReadFile");
        }
        if (read == 0) {
            break; // 讀的期間檔案變短
        }
        done += read;
    }
    bytes.truncate(done);
    return decode(bytes);
}

} // namespace auralis
