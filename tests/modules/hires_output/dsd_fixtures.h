#pragma once

// 測試時產生最小的 DSF／DFF（不把音檔放進 git）。欄位都能個別改壞，給解析器的防護測試用。

#include <QByteArray>
#include <QList>
#include <QString>

namespace auralis::test {

inline constexpr quint32 kDsd64 = 2822400;

/// DSF 檔的各個欄位；預設是合法的 DSD64 立體聲、每樣本 1 位元（最低位在前）。
struct DsfSpec {
    quint32 channels = 2;
    quint32 rate = kDsd64;
    quint32 bitsPerSample = 1;
    quint32 blockSize = 4096;
    quint32 formatId = 0;
    /// 每個聲道的 DSD 位元組（照檔案裡的位元順序），長度要相同；寫檔時補零到 4096 的倍數。
    QList<QByteArray> channelData;
    /// 以下有值時取代照內容算出的欄位（用來做溢位與不一致的檔頭）。
    qint64 sampleCount = -1;
    qint64 fmtChunkSize = -1;
    qint64 dataChunkSize = -1;
};
QByteArray makeDsf(const DsfSpec& spec);

/// DFF（DSDIFF）檔的各個欄位；預設是合法的 DSD64 立體聲、沒有壓縮。
struct DffSpec {
    quint16 channels = 2;
    quint32 rate = kDsd64;
    /// CMPR 的壓縮方式（4 個字元）。
    QByteArray compression = "DSD ";
    /// 資料區塊的名稱：沒有壓縮是 "DSD "，DST 壓縮是 "DST "。
    QByteArray dataChunkId = "DSD ";
    /// 各聲道以位元組輪流排好的 DSD 資料（檔案裡的樣子）。
    QByteArray data;
    /// 插在 PROP 前面與資料後面的其他區塊（已含 12 位元組的區塊頭）。
    QByteArray chunksBeforeProp;
    QByteArray chunksAfterData;
    /// 以下有值時取代照內容算出的長度欄位。
    qint64 formSize = -1;
    qint64 propSize = -1;
    qint64 chnlSize = -1;
    qint64 dataSize = -1;
};
QByteArray makeDff(const DffSpec& spec);

/// 一個 DSDIFF 區塊：名稱、8 位元組大端長度、內容，長度是奇數時補一個 0。
QByteArray dffChunk(const char* id, const QByteArray& body);

/// 固定種子的隨機位元組。
QByteArray randomBytes(qsizetype size, quint32 seed);

/// 測試用的空資料夾：AURALIS_TEST_TMP/t-32/<name>，先清掉上一輪留下的。
QString freshDsdDir(const QString& name);
/// 把 bytes 寫成檔案，回傳路徑。
QString writeFile(const QString& dir, const QString& fileName, const QByteArray& bytes);

} // namespace auralis::test
