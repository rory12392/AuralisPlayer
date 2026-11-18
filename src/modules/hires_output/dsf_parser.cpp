#include "dsd_stream.h"

#include <QtEndian>

#include <algorithm>
#include <array>

namespace auralis::mod::out {

namespace {

/// DSD 區塊固定 28 個位元組，fmt 區塊接在後面。
constexpr qint64 kDsdChunkBytes = 28;
/// fmt 區塊在規格裡是 52 個位元組；比這個大的照它宣告的長度找 data 區塊，但不採信離譜的值。
constexpr quint64 kFmtChunkBytes = 52;
constexpr quint64 kMaxFmtChunkBytes = 4096;
/// 區塊頭：4 個位元組的名稱加 8 個位元組的長度（長度包含區塊頭）。
constexpr qint64 kChunkHeaderBytes = 12;

} // namespace

Result<DsdStreamInfo> parseDsfHeader(DsdFile& file) {
    // DSD 區塊與 fmt 區塊（到區塊大小欄位之後的保留欄位為止）；全部小端
    std::array<quint8, 80> head{};
    if (!file.readAt(0, head.data(), static_cast<qint64>(head.size()))) {
        return corruptError("DSF 檔頭不完整");
    }
    if (!hasChunkId(&head[0], "DSD ") ||
        qFromLittleEndian<quint64>(&head[4]) != static_cast<quint64>(kDsdChunkBytes)) {
        return corruptError("DSF 的 DSD 區塊");
    }
    const quint64 fmtBytes = qFromLittleEndian<quint64>(&head[32]);
    if (!hasChunkId(&head[28], "fmt ") || fmtBytes < kFmtChunkBytes ||
        fmtBytes > kMaxFmtChunkBytes) {
        return corruptError("DSF 的 fmt 區塊");
    }
    const quint32 formatId = qFromLittleEndian<quint32>(&head[44]);
    const quint32 channels = qFromLittleEndian<quint32>(&head[52]);
    const quint32 rate = qFromLittleEndian<quint32>(&head[56]);
    const quint32 bitsPerSample = qFromLittleEndian<quint32>(&head[60]);
    const quint64 sampleCount = qFromLittleEndian<quint64>(&head[64]);
    const quint32 blockBytes = qFromLittleEndian<quint32>(&head[72]);
    if (formatId != 0) {
        return unsupportedError(QStringLiteral("不支援的 DSF 編碼方式"), "DSF 的格式代碼");
    }
    if (channels == 0 || channels > kMaxChannels) {
        return unsupportedError(QStringLiteral("不支援的聲道數"), "DSF 的聲道數");
    }
    if (!isSupportedDsdRate(rate)) {
        return unsupportedError(QStringLiteral("不支援的 DSD 取樣率"), "DSF 的取樣率");
    }
    if (bitsPerSample != 1 && bitsPerSample != 8) {
        return corruptError("DSF 的每樣本位元數");
    }
    if (blockBytes != kDsfBlockBytes) {
        return corruptError("DSF 的區塊大小");
    }

    const qint64 dataHeader = kDsdChunkBytes + static_cast<qint64>(fmtBytes);
    std::array<quint8, kChunkHeaderBytes> data{};
    if (!file.readAt(dataHeader, data.data(), kChunkHeaderBytes) || !hasChunkId(&data[0], "data")) {
        return corruptError("DSF 的 data 區塊");
    }
    const quint64 dataChunkBytes = qFromLittleEndian<quint64>(&data[4]);
    if (dataChunkBytes < static_cast<quint64>(kChunkHeaderBytes)) {
        return corruptError("DSF 的 data 區塊長度");
    }

    DsdStreamInfo info;
    info.dsdRate = rate;
    info.channels = static_cast<quint16>(channels);
    info.dataOffset = dataHeader + kChunkHeaderBytes;
    info.lsbFirst = bitsPerSample == 1;
    // 檔頭宣告的長度不能超過檔案實際有的：資料長度、樣本數都以檔案為上限，
    // 而且只算每個聲道的塊都完整存在的那幾組
    const quint64 inFile = static_cast<quint64>(file.size() - info.dataOffset);
    const quint64 dataBytes =
        std::min(dataChunkBytes - static_cast<quint64>(kChunkHeaderBytes), inFile);
    const quint64 groupBytes = quint64{channels} * static_cast<quint64>(kDsfBlockBytes);
    const quint64 framesInData = dataBytes / groupBytes * static_cast<quint64>(kDsfBlockBytes / 4);
    info.frames = static_cast<qint64>(std::min(framesInData, sampleCount / 32));
    if (info.frames == 0) {
        return unsupportedError(QStringLiteral("檔案裡沒有音訊資料"), "DSF 的資料長度");
    }
    return info;
}

} // namespace auralis::mod::out
