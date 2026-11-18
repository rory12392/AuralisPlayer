// DSF／DFF 音源：交出核心約定的 DSD 位元流（每聲道 32 個位元一框、4 位元組交錯、最高位在前）。
#include "dsd_fixtures.h"
#include "dsd_test_helpers.h"

#include <auralis/dsd.h>

#include <catch2/catch_test_macros.hpp>

using auralis::AudioFormat;
using auralis::DsdFormat;
using namespace auralis::test;

namespace {

const QByteArray kLeft = QByteArray::fromHex("0102040810204080");
const QByteArray kRight = QByteArray::fromHex("ff00d20f8001c003");

} // namespace

TEST_CASE("AC-04-1：DSF（每樣本 1 位元，最低位在前）交出最高位在前、每聲道 4 位元組交錯的位元流") {
    const QString dir = freshDsdDir(QStringLiteral("dsf-literal"));
    DsfSpec spec;
    spec.channelData = {kLeft, kRight};
    const auto source = openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec)));

    const AudioFormat format = source->format();
    CHECK(format.dsdRate == 2822400);
    CHECK(format.rate == 0);
    CHECK(format.bits == 1);
    CHECK(format.container == 32);
    CHECK(format.channels == 2);
    CHECK(format.isValid());
    CHECK(source->lengthFrames() == 2);
    // 每個位元組的位元順序反轉；第一框是左 4 個位元組、右 4 個位元組
    CHECK(readAll(*source).toHex() == QByteArray("80402010ff004bf0"
                                                 "080402010180"
                                                 "03c0"));
}

TEST_CASE("AC-04-1：DSF 每樣本 8 位元（最高位在前）時不反轉位元") {
    const QString dir = freshDsdDir(QStringLiteral("dsf-msb"));
    DsfSpec spec;
    spec.bitsPerSample = 8;
    spec.channelData = {kLeft, kRight};
    const auto source = openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec)));
    CHECK(readAll(*source).toHex() == QByteArray("01020408ff00d20f"
                                                 "102040808001c003"));
}

TEST_CASE("AC-04-1：DFF（各聲道以位元組輪流）重排成每聲道 4 位元組交錯，位元不變") {
    const QString dir = freshDsdDir(QStringLiteral("dff-literal"));
    DffSpec spec;
    spec.data = QByteArray::fromHex("11e112e213e314e4"
                                    "15e516e617e718e8");
    const auto source = openDsdOk(writeFile(dir, QStringLiteral("a.dff"), makeDff(spec)));

    const AudioFormat format = source->format();
    CHECK(format.dsdRate == 2822400);
    CHECK(format.rate == 0);
    CHECK(format.bits == 1);
    CHECK(format.container == 32);
    CHECK(format.channels == 2);
    CHECK(source->lengthFrames() == 2);
    CHECK(readAll(*source).toHex() == QByteArray("11121314e1e2e3e4"
                                                 "15161718e5e6e7e8"));
}

TEST_CASE("AC-04-1：DSD64、DSD128、DSD256 的 DSF 與 DFF 都開得起來，倍率正確") {
    const QString dir = freshDsdDir(QStringLiteral("rates"));
    struct Row {
        quint32 rate;
        quint32 multiplier;
    };
    for (const Row row : {Row{2822400, 64}, Row{5644800, 128}, Row{11289600, 256}}) {
        DYNAMIC_SECTION("DSD" << row.multiplier) {
            DsfSpec dsf;
            dsf.rate = row.rate;
            dsf.channelData = {kLeft, kRight};
            DffSpec dff;
            dff.rate = row.rate;
            dff.data = dffInterleave({kLeft, kRight});
            const auto fromDsf = openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), makeDsf(dsf)));
            const auto fromDff = openDsdOk(writeFile(dir, QStringLiteral("a.DFF"), makeDff(dff)));
            for (const auto* source : {fromDsf.get(), fromDff.get()}) {
                const DsdFormat dsd = DsdFormat::from(source->format());
                REQUIRE(dsd.isValid());
                CHECK(dsd.dsdRate == row.rate);
                CHECK(dsd.multiplier() == row.multiplier);
                CHECK(dsd.channels == 2);
            }
        }
    }
}

TEST_CASE("AC-04-1：DSF 跨多個區塊時內容與長度正確，分段讀、跳轉後讀都和從頭讀相同") {
    const QString dir = freshDsdDir(QStringLiteral("dsf-blocks"));
    // 每聲道 9192 個位元組：兩個完整的 4096 區塊加 1000 個位元組（最後一塊補零）
    const QList<QByteArray> channels{randomBytes(9192, 1), randomBytes(9192, 2)};
    DsfSpec spec;
    spec.channelData = channels;
    const QString path = writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec));
    // 9192 × 8 ÷ 32 = 2298 框
    const QByteArray expected = interleaveFrames(channels, 2298, true);

    const auto source = openDsdOk(path);
    CHECK(source->lengthFrames() == 2298);
    CHECK(readAll(*source, 333) == expected);
    const auto again = openDsdOk(path);
    CHECK(readAll(*again, 5000) == expected);

    const auto sought = source->seek(1500);
    REQUIRE(sought.hasValue());
    CHECK(sought.value() == 1500);
    CHECK(readAll(*source, 100) == expected.mid(1500 * 8));
    // 跳回前面的區塊
    REQUIRE(source->seek(7).hasValue());
    CHECK(readAll(*source, 1) == expected.mid(7 * 8));
    // 超過結尾：停在結尾，讀不到東西
    const auto past = source->seek(99999);
    REQUIRE(past.hasValue());
    CHECK(past.value() == 2298);
    CHECK(readAll(*source).isEmpty());
}

TEST_CASE("AC-04-1：DSF 的樣本數不是 32 的倍數時，長度取完整的樣本框") {
    const QString dir = freshDsdDir(QStringLiteral("dsf-partial"));
    const QList<QByteArray> channels{randomBytes(9192, 3), randomBytes(9192, 4)};
    DsfSpec spec;
    spec.channelData = channels;
    spec.sampleCount = 73531; // 73531 ÷ 32 = 2297 餘 27
    const auto source = openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec)));
    CHECK(source->lengthFrames() == 2297);
    CHECK(readAll(*source) == interleaveFrames(channels, 2297, true));
}

TEST_CASE("AC-04-1：DFF 長資料分段讀與跳轉正確，前後的其他區塊（註解、ID3）不影響") {
    const QString dir = freshDsdDir(QStringLiteral("dff-long"));
    // 10000 框又多 5 個位元組（不足一框的尾巴不算）
    const QList<QByteArray> channels{randomBytes(40000, 5), randomBytes(40000, 6)};
    DffSpec spec;
    spec.data = dffInterleave(channels) + QByteArray("\x01\x02\x03\x04\x05", 5);
    spec.chunksBeforeProp = dffChunk("COMT", QByteArray("odd", 3)); // 長度是奇數，後面補一個位元組
    spec.chunksAfterData = dffChunk("ID3 ", randomBytes(300, 7));
    const QString path = writeFile(dir, QStringLiteral("a.dff"), makeDff(spec));
    const QByteArray expected = interleaveFrames(channels, 10000, false);

    const auto source = openDsdOk(path);
    CHECK(source->lengthFrames() == 10000);
    CHECK(readAll(*source, 333) == expected);
    const auto sought = source->seek(4097);
    REQUIRE(sought.hasValue());
    CHECK(sought.value() == 4097);
    CHECK(readAll(*source, 9000) == expected.mid(4097 * 8));
    const auto past = source->seek(10001);
    REQUIRE(past.hasValue());
    CHECK(past.value() == 10000);
    CHECK(readAll(*source).isEmpty());
}

TEST_CASE("AC-04-1：單聲道與 6 聲道的 DSF、DFF 聲道順序正確") {
    const QString dir = freshDsdDir(QStringLiteral("channels"));
    for (const int count : {1, 6}) {
        DYNAMIC_SECTION(count << " 聲道") {
            QList<QByteArray> channels;
            for (int ch = 0; ch < count; ++ch) {
                channels.append(randomBytes(5000, 10 + ch));
            }
            DsfSpec dsf;
            dsf.channels = count;
            dsf.channelData = channels;
            const auto fromDsf = openDsdOk(writeFile(dir, QStringLiteral("a.dsf"), makeDsf(dsf)));
            CHECK(fromDsf->format().channels == count);
            CHECK(fromDsf->lengthFrames() == 1250);
            CHECK(readAll(*fromDsf) == interleaveFrames(channels, 1250, true));

            DffSpec dff;
            dff.channels = static_cast<quint16>(count);
            dff.data = dffInterleave(channels);
            const auto fromDff = openDsdOk(writeFile(dir, QStringLiteral("a.dff"), makeDff(dff)));
            CHECK(fromDff->format().channels == count);
            CHECK(fromDff->lengthFrames() == 1250);
            CHECK(readAll(*fromDff) == interleaveFrames(channels, 1250, false));
        }
    }
}
