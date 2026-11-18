#include "dsd_fixtures.h"

#include <QDir>
#include <QFile>
#include <QtEndian>

#include <random>
#include <stdexcept>

namespace auralis::test {

namespace {

template <typename T> void putLe(QByteArray& out, T value) {
    char bytes[sizeof(T)];
    qToLittleEndian(value, bytes);
    out.append(bytes, sizeof(T));
}

template <typename T> void putBe(QByteArray& out, T value) {
    char bytes[sizeof(T)];
    qToBigEndian(value, bytes);
    out.append(bytes, sizeof(T));
}

quint64 orDefault(qint64 override, quint64 computed) {
    return override >= 0 ? static_cast<quint64>(override) : computed;
}

} // namespace

QByteArray makeDsf(const DsfSpec& spec) {
    constexpr qsizetype kBlock = 4096;
    const qsizetype perChannel = spec.channelData.isEmpty() ? 0 : spec.channelData.first().size();
    const qsizetype blocks = (perChannel + kBlock - 1) / kBlock;
    QByteArray data;
    for (qsizetype block = 0; block < blocks; ++block) {
        for (const QByteArray& channel : spec.channelData) {
            QByteArray piece = channel.mid(block * kBlock, kBlock);
            piece.append(QByteArray(kBlock - piece.size(), '\0'));
            data.append(piece);
        }
    }
    QByteArray out;
    out.append("DSD ", 4);
    putLe<quint64>(out, 28);
    putLe<quint64>(out, static_cast<quint64>(28 + 52 + 12 + data.size()));
    putLe<quint64>(out, 0);
    out.append("fmt ", 4);
    putLe<quint64>(out, orDefault(spec.fmtChunkSize, 52));
    putLe<quint32>(out, 1);
    putLe<quint32>(out, spec.formatId);
    putLe<quint32>(out, 2);
    putLe<quint32>(out, spec.channels);
    putLe<quint32>(out, spec.rate);
    putLe<quint32>(out, spec.bitsPerSample);
    putLe<quint64>(out, orDefault(spec.sampleCount, static_cast<quint64>(perChannel) * 8));
    putLe<quint32>(out, spec.blockSize);
    putLe<quint32>(out, 0);
    out.append("data", 4);
    putLe<quint64>(out, orDefault(spec.dataChunkSize, static_cast<quint64>(12 + data.size())));
    out.append(data);
    return out;
}

QByteArray dffChunk(const char* id, const QByteArray& body) {
    QByteArray out(id, 4);
    putBe<quint64>(out, static_cast<quint64>(body.size()));
    out.append(body);
    if (body.size() % 2 != 0) {
        out.append('\0');
    }
    return out;
}

QByteArray makeDff(const DffSpec& spec) {
    QByteArray rate;
    putBe<quint32>(rate, spec.rate);
    QByteArray channels;
    putBe<quint16>(channels, spec.channels);
    for (int ch = 0; ch < qMin<int>(spec.channels, 8); ++ch) {
        channels.append(ch == 0 ? "SLFT" : ch == 1 ? "SRGT" : "C000", 4);
    }
    const QByteArray compressionName("not compressed.", 15);
    QByteArray compression = spec.compression;
    compression.append(static_cast<char>(compressionName.size()));
    compression.append(compressionName);

    QByteArray chnl("CHNL", 4);
    putBe<quint64>(chnl, orDefault(spec.chnlSize, static_cast<quint64>(channels.size())));
    chnl.append(channels);
    const QByteArray propBody =
        QByteArray("SND ", 4) + dffChunk("FS  ", rate) + chnl + dffChunk("CMPR", compression);

    QByteArray body("DSD ", 4);
    QByteArray version;
    putBe<quint32>(version, 0x01050000);
    body.append(dffChunk("FVER", version));
    body.append(spec.chunksBeforeProp);
    body.append("PROP", 4);
    putBe<quint64>(body, orDefault(spec.propSize, static_cast<quint64>(propBody.size())));
    body.append(propBody);
    body.append(spec.dataChunkId.constData(), 4);
    putBe<quint64>(body, orDefault(spec.dataSize, static_cast<quint64>(spec.data.size())));
    body.append(spec.data);
    if (spec.data.size() % 2 != 0) {
        body.append('\0');
    }
    body.append(spec.chunksAfterData);

    QByteArray out("FRM8", 4);
    putBe<quint64>(out, orDefault(spec.formSize, static_cast<quint64>(body.size())));
    out.append(body);
    return out;
}

QByteArray randomBytes(qsizetype size, quint32 seed) {
    QByteArray bytes(size, Qt::Uninitialized);
    std::mt19937 random(seed);
    for (char& byte : bytes) {
        byte = static_cast<char>(random() & 0xFF);
    }
    return bytes;
}

QString freshDsdDir(const QString& name) {
    const QString path = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-32/") + name;
    QDir(path).removeRecursively();
    if (!QDir().mkpath(path)) {
        throw std::runtime_error("無法建立測試資料夾：" + path.toStdString());
    }
    return QDir::toNativeSeparators(path);
}

QString writeFile(const QString& dir, const QString& fileName, const QByteArray& bytes) {
    const QString path = dir + QLatin1Char('\\') + fileName;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error("無法寫入測試檔：" + path.toStdString());
    }
    return path;
}

} // namespace auralis::test
