// DSF／DFF 解析器的防護：截斷、長度欄位溢位、DST 壓縮、聲道數異常都回傳原因，
// 不讓程式結束，也不讀超過檔案。
#include "dsd_fixtures.h"
#include "dsd_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

using namespace auralis::test;

namespace {

/// 一個合法的 DSF：立體聲、每聲道 groups 個 4096 區塊的隨機資料。
struct ValidDsf {
    QList<QByteArray> channels;
    QByteArray bytes;
};
ValidDsf validDsf(int groups) {
    ValidDsf file;
    file.channels = {randomBytes(4096 * groups, 21), randomBytes(4096 * groups, 22)};
    DsfSpec spec;
    spec.channelData = file.channels;
    file.bytes = makeDsf(spec);
    return file;
}

/// 一個合法的 DFF：立體聲、frames 個樣本框的隨機資料。
struct ValidDff {
    QList<QByteArray> channels;
    QByteArray bytes;
    qsizetype dataStart = 0;
};
ValidDff validDff(int frames) {
    ValidDff file;
    file.channels = {randomBytes(frames * 4, 23), randomBytes(frames * 4, 24)};
    DffSpec spec;
    spec.data = dffInterleave(file.channels);
    file.bytes = makeDff(spec);
    file.dataStart = file.bytes.size() - spec.data.size();
    return file;
}

QByteArray patched(QByteArray bytes, qsizetype offset, const QByteArray& value) {
    bytes.replace(offset, value.size(), value);
    return bytes;
}

const QByteArray kAllOnes(8, '\xff');

/// 開檔要失敗並帶著給使用者看的原因。
QString rejected(const QString& dir, const QString& fileName, const QByteArray& bytes) {
    const auto opened = openDsd(writeFile(dir, fileName, bytes));
    REQUIRE_FALSE(opened.hasValue());
    REQUIRE_FALSE(opened.error().message.isEmpty());
    return opened.error().message;
}

/// 開得起來的檔案：宣告的長度不超過檔案實際有的位元組，而且讀得到結尾。
void requireReadableWithinFile(const QString& path, qint64 fileSize) {
    const auto opened = openDsd(path);
    if (!opened) {
        CHECK_FALSE(opened.error().message.isEmpty());
        return;
    }
    auralis::IAudioSource& source = *opened.value();
    const qint64 frameBytes = source.format().bytesPerFrame();
    REQUIRE(source.format().isValid());
    REQUIRE(source.lengthFrames() > 0);
    REQUIRE(source.lengthFrames() * frameBytes <= fileSize);
    REQUIRE(readAll(source, 4096).size() == source.lengthFrames() * frameBytes);
}

} // namespace

TEST_CASE("AC-04-1：檔頭被截斷的 DSF／DFF 回傳原因，不讓程式結束") {
    const QString dir = freshDsdDir(QStringLiteral("truncated-header"));
    const ValidDsf dsf = validDsf(1);
    // DSF 檔頭：DSD 區塊 28＋fmt 區塊 52＋data 區塊頭 12 = 92 個位元組
    for (qsizetype length = 0; length < 92; ++length) {
        INFO("DSF 長度 " << length);
        rejected(dir, QStringLiteral("a.dsf"), dsf.bytes.left(length));
    }
    const ValidDff dff = validDff(64);
    for (qsizetype length = 0; length < dff.dataStart; ++length) {
        INFO("DFF 長度 " << length);
        rejected(dir, QStringLiteral("a.dff"), dff.bytes.left(length));
    }
}

TEST_CASE("AC-04-1：資料被截斷的 DSF／DFF 只交出檔案裡完整存在的部分") {
    const QString dir = freshDsdDir(QStringLiteral("truncated-data"));
    // DSF 3 組區塊，截到剩一組半：只有第一組（1024 框）完整
    const ValidDsf dsf = validDsf(3);
    const auto fromDsf =
        openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), dsf.bytes.left(92 + 8192 + 4096)));
    CHECK(fromDsf->lengthFrames() == 1024);
    CHECK(readAll(*fromDsf) == interleaveFrames(dsf.channels, 1024, true));

    // DFF 1000 框，截到剩 250 框又 3 個位元組
    const ValidDff dff = validDff(1000);
    const auto fromDff = openDsdOk(
        writeFile(dir, QStringLiteral("a.dff"), dff.bytes.left(dff.dataStart + 250 * 8 + 3)));
    CHECK(fromDff->lengthFrames() == 250);
    CHECK(readAll(*fromDff) == interleaveFrames(dff.channels, 250, false));

    // 資料一個完整的樣本框都沒有
    CHECK(rejected(dir, QStringLiteral("b.dsf"), dsf.bytes.left(92 + 100))
              .contains(QStringLiteral("沒有音訊")));
    CHECK(rejected(dir, QStringLiteral("b.dff"), dff.bytes.left(dff.dataStart + 7))
              .contains(QStringLiteral("沒有音訊")));
}

TEST_CASE("AC-04-1：DSF 的長度欄位溢位時不採信，以檔案實際長度為準或回傳原因") {
    const QString dir = freshDsdDir(QStringLiteral("overflow-dsf"));
    const ValidDsf dsf = validDsf(2);
    const QByteArray expected = interleaveFrames(dsf.channels, 2048, true);
    const auto path = [&](const QByteArray& bytes) {
        return writeFile(dir, QStringLiteral("a.dsf"), bytes);
    };

    SECTION("樣本數全是 1：長度收斂到檔案裡的資料") {
        const auto source = openDsdOk(path(patched(dsf.bytes, 64, kAllOnes)));
        CHECK(source->lengthFrames() == 2048);
        CHECK(readAll(*source) == expected);
    }
    SECTION("data 區塊長度全是 1：長度收斂到檔案裡的資料") {
        const auto source = openDsdOk(path(patched(dsf.bytes, 84, kAllOnes)));
        CHECK(source->lengthFrames() == 2048);
        CHECK(readAll(*source) == expected);
    }
    SECTION("data 區塊長度比區塊頭還小") {
        CHECK(rejected(dir, QStringLiteral("a.dsf"), patched(dsf.bytes, 84, QByteArray(8, '\0')))
                  .contains(QStringLiteral("損毀")));
    }
    SECTION("fmt 區塊長度全是 1") {
        CHECK(rejected(dir, QStringLiteral("a.dsf"), patched(dsf.bytes, 32, kAllOnes))
                  .contains(QStringLiteral("損毀")));
    }
    SECTION("DSD 區塊長度全是 1") {
        CHECK(rejected(dir, QStringLiteral("a.dsf"), patched(dsf.bytes, 4, kAllOnes))
                  .contains(QStringLiteral("損毀")));
    }
    SECTION("每聲道區塊大小不是 4096") {
        for (const quint32 blockSize : {0u, 1u, 8192u, 0xFFFFFFFFu}) {
            DsfSpec spec;
            spec.channelData = dsf.channels;
            spec.blockSize = blockSize;
            INFO(blockSize);
            rejected(dir, QStringLiteral("a.dsf"), makeDsf(spec));
        }
    }
}

TEST_CASE("AC-04-1：DFF 的區塊長度溢位時不採信，以檔案實際長度為準或回傳原因") {
    const QString dir = freshDsdDir(QStringLiteral("overflow-dff"));
    const ValidDff dff = validDff(500);
    const QByteArray expected = interleaveFrames(dff.channels, 500, false);
    const auto offsetAfter = [&](const char* id) {
        return dff.bytes.indexOf(id) + 4;
    };

    SECTION("FRM8 長度全是 1：以檔案長度為準") {
        const auto source =
            openDsdOk(writeFile(dir, QStringLiteral("a.dff"), patched(dff.bytes, 4, kAllOnes)));
        CHECK(source->lengthFrames() == 500);
        CHECK(readAll(*source) == expected);
    }
    SECTION("DSD 資料區塊長度全是 1：以檔案長度為準") {
        const auto source = openDsdOk(writeFile(dir, QStringLiteral("a.dff"),
                                                patched(dff.bytes, dff.dataStart - 8, kAllOnes)));
        CHECK(source->lengthFrames() == 500);
        CHECK(readAll(*source) == expected);
    }
    SECTION("PROP 與它的子區塊長度全是 1") {
        for (const char* id : {"PROP", "FS  ", "CHNL", "CMPR", "FVER"}) {
            INFO(id);
            CHECK(rejected(dir, QStringLiteral("a.dff"),
                           patched(dff.bytes, offsetAfter(id), kAllOnes))
                      .contains(QStringLiteral("損毀")));
        }
    }
    SECTION("PROP 宣告的長度超過上限（不照檔頭配置記憶體）") {
        DffSpec spec;
        spec.data = dffInterleave(dff.channels);
        spec.propSize = qint64{1} << 40;
        rejected(dir, QStringLiteral("a.dff"), makeDff(spec));
    }
    SECTION("CHNL 的長度比聲道數欄位還小") {
        DffSpec spec;
        spec.data = dffInterleave(dff.channels);
        spec.chnlSize = 1;
        rejected(dir, QStringLiteral("a.dff"), makeDff(spec));
    }
    SECTION("區塊數量異常多（全是空區塊）") {
        DffSpec spec;
        spec.data = dffInterleave(dff.channels);
        for (int i = 0; i < 5000; ++i) {
            spec.chunksBeforeProp.append(dffChunk("JUNK", {}));
        }
        CHECK(
            rejected(dir, QStringLiteral("a.dff"), makeDff(spec)).contains(QStringLiteral("損毀")));
    }
}

TEST_CASE("模組規格 §1：DST 壓縮的 DFF 回傳原因並跳過，不當成 DSD 資料播放") {
    const QString dir = freshDsdDir(QStringLiteral("dst"));
    const QByteArray payload = randomBytes(4000, 31);

    SECTION("CMPR 宣告 DST") {
        DffSpec spec;
        spec.compression = "DST ";
        spec.dataChunkId = "DST ";
        spec.data = payload;
        CHECK(rejected(dir, QStringLiteral("a.dff"), makeDff(spec)) ==
              QStringLiteral("DST 壓縮的 DFF 不支援"));
    }
    SECTION("CMPR 宣告沒有壓縮，資料卻是 DST 區塊") {
        DffSpec spec;
        spec.dataChunkId = "DST ";
        spec.data = payload;
        CHECK(rejected(dir, QStringLiteral("a.dff"), makeDff(spec)) ==
              QStringLiteral("DST 壓縮的 DFF 不支援"));
    }
    SECTION("其他不認得的壓縮方式") {
        DffSpec spec;
        spec.compression = "XYZ ";
        spec.data = payload;
        CHECK(
            rejected(dir, QStringLiteral("a.dff"), makeDff(spec)).contains(QStringLiteral("壓縮")));
    }
}

TEST_CASE("AC-04-1：聲道數異常的 DSF／DFF 回傳原因") {
    const QString dir = freshDsdDir(QStringLiteral("bad-channels"));
    const ValidDsf dsf = validDsf(1);
    for (const quint32 channels : {0u, 7u, 1000u, 0xFFFFFFFFu}) {
        DsfSpec spec;
        spec.channelData = dsf.channels;
        spec.channels = channels;
        INFO("DSF " << channels);
        CHECK(
            rejected(dir, QStringLiteral("a.dsf"), makeDsf(spec)).contains(QStringLiteral("聲道")));
    }
    for (const quint16 channels : {quint16{0}, quint16{7}, quint16{65535}}) {
        DffSpec spec;
        spec.data = randomBytes(8000, 32);
        spec.channels = channels;
        INFO("DFF " << channels);
        CHECK(
            rejected(dir, QStringLiteral("a.dff"), makeDff(spec)).contains(QStringLiteral("聲道")));
    }
}

TEST_CASE("AC-04-1：不支援的取樣率與格式欄位回傳原因") {
    const QString dir = freshDsdDir(QStringLiteral("bad-format"));
    const ValidDsf dsf = validDsf(1);
    for (const quint32 rate : {0u, 44100u, 3072000u, 22579200u, 0xFFFFFFFFu}) {
        DsfSpec spec;
        spec.channelData = dsf.channels;
        spec.rate = rate;
        DffSpec dff;
        dff.data = randomBytes(8000, 33);
        dff.rate = rate;
        INFO(rate);
        CHECK(rejected(dir, QStringLiteral("a.dsf"), makeDsf(spec))
                  .contains(QStringLiteral("取樣率")));
        CHECK(rejected(dir, QStringLiteral("a.dff"), makeDff(dff))
                  .contains(QStringLiteral("取樣率")));
    }
    DsfSpec notRaw;
    notRaw.channelData = dsf.channels;
    notRaw.formatId = 1;
    rejected(dir, QStringLiteral("a.dsf"), makeDsf(notRaw));
    DsfSpec badBits;
    badBits.channelData = dsf.channels;
    badBits.bitsPerSample = 16;
    rejected(dir, QStringLiteral("a.dsf"), makeDsf(badBits));
}

TEST_CASE("AC-04-1：內容不是 DSD、空檔案、不存在的檔案回傳原因") {
    const QString dir = freshDsdDir(QStringLiteral("not-dsd"));
    const QByteArray junk = randomBytes(20000, 34);
    CHECK(rejected(dir, QStringLiteral("a.dsf"), junk).contains(QStringLiteral("損毀")));
    CHECK(rejected(dir, QStringLiteral("a.dff"), junk).contains(QStringLiteral("損毀")));
    rejected(dir, QStringLiteral("empty.dsf"), {});
    rejected(dir, QStringLiteral("empty.dff"), {});
    // 副檔名和內容不符（.dsf 裡是 DFF）
    rejected(dir, QStringLiteral("swapped.dsf"), validDff(64).bytes);
    rejected(dir, QStringLiteral("swapped.dff"), validDsf(1).bytes);

    const auto missing = openDsd(dir + QStringLiteral("\\不存在.dsf"));
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message == QStringLiteral("找不到檔案"));
}

TEST_CASE("AC-04-1：檔頭任何一個位元組被改壞，都不會讓程式結束或讀超過檔案") {
    const QString dir = freshDsdDir(QStringLiteral("mutation"));
    const ValidDsf dsf = validDsf(2);
    const ValidDff dff = validDff(300);
    for (const char value : {'\x00', '\xff', '\x7f', '\x80'}) {
        for (qsizetype offset = 0; offset < 92; ++offset) {
            INFO("DSF 位置 " << offset << " 改成 " << int(static_cast<unsigned char>(value)));
            const QByteArray bytes = patched(dsf.bytes, offset, QByteArray(1, value));
            requireReadableWithinFile(writeFile(dir, QStringLiteral("m.dsf"), bytes), bytes.size());
        }
        for (qsizetype offset = 0; offset < dff.dataStart; ++offset) {
            INFO("DFF 位置 " << offset << " 改成 " << int(static_cast<unsigned char>(value)));
            const QByteArray bytes = patched(dff.bytes, offset, QByteArray(1, value));
            requireReadableWithinFile(writeFile(dir, QStringLiteral("m.dff"), bytes), bytes.size());
        }
    }
}

TEST_CASE("AC-04-1：開啟後檔案被截短時，讀取回傳錯誤而不是交出不存在的資料") {
    const QString dir = freshDsdDir(QStringLiteral("shrunk"));
    const ValidDff dff = validDff(20000);
    const QString path = writeFile(dir, QStringLiteral("a.dff"), dff.bytes);
    const auto source = openDsdOk(path);
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.resize(dff.dataStart + 800));
    }
    QByteArray buffer(8 * 4096, '\0');
    // 前 100 框還在檔案裡
    const auto first = source->read(reinterpret_cast<std::byte*>(buffer.data()), 100);
    REQUIRE(first.hasValue());
    CHECK(first.value() == 100);
    REQUIRE(source->seek(5000).hasValue());
    const auto beyond = source->read(reinterpret_cast<std::byte*>(buffer.data()), 4096);
    REQUIRE_FALSE(beyond.hasValue());
    CHECK(beyond.error().message.contains(QStringLiteral("讀取")));
}

TEST_CASE("資料安全：播放中的 DSD 檔可以被搬移，內容與修改時間不變（只讀不改）") {
    const QString dir = freshDsdDir(QStringLiteral("read-only"));
    const ValidDsf dsf = validDsf(2);
    const QString path = writeFile(dir, QStringLiteral("原檔.dsf"), dsf.bytes);
    const QDateTime modified = QFileInfo(path).lastModified();

    const auto source = openDsdOk(path);
    QByteArray buffer(8 * 500, '\0');
    REQUIRE(source->read(reinterpret_cast<std::byte*>(buffer.data()), 500).hasValue());
    const QString moved = dir + QStringLiteral("\\搬走.dsf");
    // 開著的時候別的程式（這裡是測試自己）可以改名
    REQUIRE(QFile::rename(path, moved));
    // 搬走後繼續讀到結尾
    CHECK(readAll(*source).size() == (2048 - 500) * 8);

    QFile after(moved);
    REQUIRE(after.open(QIODevice::ReadOnly));
    CHECK(after.readAll() == dsf.bytes);
    CHECK(QFileInfo(moved).lastModified() == modified);
}
