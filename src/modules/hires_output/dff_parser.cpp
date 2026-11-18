#include "dsd_stream.h"

#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace auralis::mod::out {

namespace {

/// 區塊頭：4 個位元組的名稱加 8
/// 個位元組的長度（大端，長度不含區塊頭；內容是奇數長時後面補一個位元組）。
constexpr qint64 kChunkHeaderBytes = 12;
/// PROP 區塊整個讀進記憶體，所以設上限（正常只有幾十到幾百個位元組）。
constexpr quint64 kMaxPropBytes = 64 * 1024;
/// 找 PROP 與資料區塊時最多看幾個區塊；超過視為損毀（防止用大量小區塊拖住開檔）。
constexpr int kMaxChunks = 1024;

struct SoundProperties {
    bool hasRate = false;
    bool hasChannels = false;
    bool hasCompression = false;
    quint32 rate = 0;
    quint32 channels = 0;
    std::array<quint8, 4> compression{};
};

/// 解析 PROP 區塊的內容（從型別 "SND " 起）。子區塊的長度超出 PROP 時回傳損毀。
Result<SoundProperties> parseProperties(const std::vector<quint8>& prop) {
    const quint64 size = prop.size();
    if (size < 4 || !hasChunkId(prop.data(), "SND ")) {
        return corruptError("DFF 的 PROP 型別");
    }
    SoundProperties properties;
    quint64 pos = 4;
    while (pos <= size && size - pos >= static_cast<quint64>(kChunkHeaderBytes)) {
        const quint8* header = prop.data() + pos;
        const quint64 bytes = qFromBigEndian<quint64>(header + 4);
        pos += static_cast<quint64>(kChunkHeaderBytes);
        if (bytes > size - pos) {
            return corruptError("DFF 的 PROP 子區塊長度");
        }
        const quint8* body = prop.data() + pos;
        if (hasChunkId(header, "FS  ")) {
            if (bytes < 4) {
                return corruptError("DFF 的 FS 區塊");
            }
            properties.rate = qFromBigEndian<quint32>(body);
            properties.hasRate = true;
        } else if (hasChunkId(header, "CHNL")) {
            if (bytes < 2) {
                return corruptError("DFF 的 CHNL 區塊");
            }
            properties.channels = qFromBigEndian<quint16>(body);
            properties.hasChannels = true;
        } else if (hasChunkId(header, "CMPR")) {
            if (bytes < 4) {
                return corruptError("DFF 的 CMPR 區塊");
            }
            std::memcpy(properties.compression.data(), body, 4);
            properties.hasCompression = true;
        }
        pos += bytes + (bytes & 1);
    }
    return properties;
}

Error dstError() {
    return unsupportedError(QStringLiteral("DST 壓縮的 DFF 不支援"), "DFF 的壓縮方式");
}

} // namespace

Result<DsdStreamInfo> parseDffHeader(DsdFile& file) {
    std::array<quint8, 16> head{};
    if (!file.readAt(0, head.data(), static_cast<qint64>(head.size())) ||
        !hasChunkId(&head[0], "FRM8") || !hasChunkId(&head[12], "DSD ")) {
        return corruptError("DFF 的 FRM8 區塊");
    }
    // 容器宣告的結尾不能超過檔案實際的結尾
    const quint64 formBytes = qFromBigEndian<quint64>(&head[4]);
    qint64 end = file.size();
    if (formBytes < static_cast<quint64>(end - kChunkHeaderBytes)) {
        end = kChunkHeaderBytes + static_cast<qint64>(formBytes);
    }

    SoundProperties properties;
    bool hasProperties = false;
    bool hasData = false;
    bool hasDst = false;
    qint64 dataOffset = 0;
    quint64 dataBytes = 0;
    qint64 pos = static_cast<qint64>(head.size());
    for (int chunk = 0; pos <= end - kChunkHeaderBytes; ++chunk) {
        if (chunk >= kMaxChunks) {
            return corruptError("DFF 的區塊數量");
        }
        std::array<quint8, kChunkHeaderBytes> header{};
        if (!file.readAt(pos, header.data(), kChunkHeaderBytes)) {
            return corruptError("DFF 的區塊頭");
        }
        const quint64 bytes = qFromBigEndian<quint64>(&header[4]);
        const qint64 body = pos + kChunkHeaderBytes;
        const quint64 left = static_cast<quint64>(end - body);
        if (hasChunkId(&header[0], "DST ")) {
            hasDst = true;
            break;
        }
        if (hasChunkId(&header[0], "DSD ")) {
            // 資料區塊宣告的長度超過檔案時，只用檔案裡實際有的部分
            dataOffset = body;
            dataBytes = std::min(bytes, left);
            hasData = true;
        } else if (hasChunkId(&header[0], "PROP") && !hasProperties) {
            if (bytes > left || bytes > kMaxPropBytes) {
                return corruptError("DFF 的 PROP 區塊長度");
            }
            std::vector<quint8> prop(static_cast<std::size_t>(bytes));
            if (!file.readAt(body, prop.data(), static_cast<qint64>(bytes))) {
                return corruptError("DFF 的 PROP 區塊");
            }
            auto parsed = parseProperties(prop);
            if (!parsed) {
                return parsed.error();
            }
            properties = parsed.value();
            hasProperties = true;
        }
        if ((hasProperties && hasData) || bytes > left) {
            // 兩個都找到了；或這個區塊超出檔案，後面沒有可信的內容
            break;
        }
        pos = body + static_cast<qint64>(bytes + (bytes & 1));
    }

    if (hasDst ||
        (properties.hasCompression && hasChunkId(properties.compression.data(), "DST "))) {
        return dstError();
    }
    if (!hasProperties || !properties.hasRate || !properties.hasChannels ||
        !properties.hasCompression || !hasData) {
        return corruptError("DFF 缺少必要的區塊");
    }
    if (!hasChunkId(properties.compression.data(), "DSD ")) {
        return unsupportedError(QStringLiteral("不支援的壓縮方式"), "DFF 的壓縮方式");
    }
    if (properties.channels == 0 || properties.channels > kMaxChannels) {
        return unsupportedError(QStringLiteral("不支援的聲道數"), "DFF 的聲道數");
    }
    if (!isSupportedDsdRate(properties.rate)) {
        return unsupportedError(QStringLiteral("不支援的 DSD 取樣率"), "DFF 的取樣率");
    }

    DsdStreamInfo info;
    info.dsdRate = properties.rate;
    info.channels = static_cast<quint16>(properties.channels);
    info.dataOffset = dataOffset;
    info.frames = static_cast<qint64>(dataBytes / properties.channels / 4);
    if (info.frames == 0) {
        return unsupportedError(QStringLiteral("檔案裡沒有音訊資料"), "DFF 的資料長度");
    }
    return info;
}

} // namespace auralis::mod::out
