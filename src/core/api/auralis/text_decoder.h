#pragma once

#include <auralis/result.h>

#include <QByteArray>
#include <QString>

namespace auralis {

/// 解碼後的文字與判斷出的編碼。
struct DecodedText {
    QString text;
    /// 判斷出的編碼：UTF-8、UTF-16LE、UTF-16BE、Shift_JIS（CP932）、Big5（CP950）、
    /// 其他 uchardet 認得的名稱，或系統 ANSI 字碼頁（例：CP950）。給日誌與測試，不給使用者看。
    QString encoding;
    /// 開頭有 BOM（BOM 本身不在 text 裡）。
    bool hadBom = false;
};

/// 讀文字檔並偵測編碼：BOM（UTF-8、UTF-16）→ 嚴格 UTF-8（純 ASCII 也算）→ uchardet
/// （Shift_JIS、Big5 等）→ 系統 ANSI 字碼頁。.cue、.lrc、.m3u、.pls 共用（AC-07-3）。
/// 換行不轉換（\r\n 照樣留在 text 裡），由呼叫端依格式自己切行。
class TextDecoder {
public:
    /// 文字檔的大小上限（位元組）；超過時 decodeFile 回傳錯誤，避免把誤選的大檔整個讀進記憶體。
    static constexpr qint64 kMaxFileBytes = 32 * 1024 * 1024;

    /// 唯讀開啟（共用讀寫刪除）並解碼。讀不到或超過大小上限時回傳 Error。
    /// 執行緒：任何非 UI 執行緒都可以（會讀檔）；可以同時呼叫。
    static Result<DecodedText> decodeFile(const QString& path);

    /// 解碼一段位元組（規則同 decodeFile）。執行緒：任何執行緒。
    static DecodedText decode(const QByteArray& bytes);
};

} // namespace auralis
