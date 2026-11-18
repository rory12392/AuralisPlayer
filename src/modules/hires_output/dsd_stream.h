#pragma once

#include "dsd_file.h"

#include <auralis/result.h>

#include <QtGlobal>

namespace auralis::mod::out {

/// 檔頭解析的結果：DSD 資料在檔案的哪裡、怎麼排。
/// 解析器保證 frames 個樣本框對應的位元組都在檔案長度之內（檔頭宣告的長度超過檔案時以檔案為準）。
struct DsdStreamInfo {
    /// DSD 取樣率（Hz）：2822400、5644800 或 11289600。
    quint32 dsdRate = 0;
    quint16 channels = 0;
    /// DSD 資料第一個位元組在檔案中的位置。
    qint64 dataOffset = 0;
    /// 可以讀的樣本框數（每聲道 32 個位元為一框）。
    qint64 frames = 0;
    /// 每個位元組裡時間最早的位元在最低位（DSF 的每樣本 1 位元）；讀檔時反轉成最高位在前。
    bool lsbFirst = false;
};

/// 聲道數上限（DSF 規格最多 6 聲道）。
inline constexpr quint32 kMaxChannels = 6;
/// DSF 每個聲道一塊的位元組數（規格固定）；一塊是 1024 個樣本框。
inline constexpr qint64 kDsfBlockBytes = 4096;

/// 支援的 DSD 取樣率：DSD64、DSD128、DSD256（AC-04-1）。
constexpr bool isSupportedDsdRate(quint32 rate) noexcept {
    return rate == 2822400 || rate == 5644800 || rate == 11289600;
}

/// 檔頭不完整或欄位不合理時的錯誤。message 給使用者看，where（哪個欄位）只進日誌。
inline Error corruptError(const char* where) {
    return Error{QStringLiteral("檔案已損毀或格式不支援"), 0, QString::fromUtf8(where)};
}
/// 格式本身看得懂、但不在支援範圍時的錯誤（reason 給使用者看）。
inline Error unsupportedError(const QString& reason, const char* where) {
    return Error{reason, 0, QString::fromUtf8(where)};
}
/// bytes 開頭的 4 個位元組是不是區塊名稱 id。
inline bool hasChunkId(const quint8* bytes, const char* id) noexcept {
    return bytes[0] == static_cast<quint8>(id[0]) && bytes[1] == static_cast<quint8>(id[1]) &&
           bytes[2] == static_cast<quint8>(id[2]) && bytes[3] == static_cast<quint8>(id[3]);
}

/// 解析 DSF 的 DSD／fmt／data 區塊。檔頭不完整、欄位不合理或格式不支援時回傳原因。
Result<DsdStreamInfo> parseDsfHeader(DsdFile& file);
/// 解析 DSDIFF 的 FRM8／PROP／DSD 區塊。DST 壓縮回傳「不支援」的原因。
Result<DsdStreamInfo> parseDffHeader(DsdFile& file);

} // namespace auralis::mod::out
