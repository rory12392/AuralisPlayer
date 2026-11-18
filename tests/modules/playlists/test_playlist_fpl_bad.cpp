// .fpl 的壞檔（T-53，US-13 AC-13-3；自寫二進位解析的信任邊界）：程式產生的壞檔涵蓋每一種
// 結構檢查（空檔、標頭錯、截斷、字串表長度、count、位移越界、找不到 NUL、E 不符、乘法溢位、
// 檔尾多出、解碼總量、檔案超過讀取上限），都回「不支援」或「讀不到」，不 crash、不建立清單。
#include "pl_fpl_test_helpers.h"
#include "pl_import_test_helpers.h"
#include "playlist_parse.h"

#include <QElapsedTimer>
#include <QRandomGenerator>

using auralis::mod::pl::parseFpl;
using auralis::mod::pl::readPlaylistFile;
using auralis::test::appendU32;
using auralis::test::buildFpl;
using auralis::test::fixture;
using auralis::test::kFplHeader;
using auralis::test::oneTrack;
using auralis::test::OneTrack;
using auralis::test::Samples;
using auralis::test::setU32;
using auralis::test::u32At;
using Fixture = auralis::test::ImportFixture;

namespace {
bool unsupported(const QByteArray& bytes) {
    return auralis::test::fplUnsupported(bytes);
}

/// 隨機改壞 test1.fpl 的 1–4 個位元組 3000 次，只要不 crash。
void fuzzTest1(Samples samples) {
    QFile file(fixture(samples, QStringLiteral("test1.fpl")));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    QRandomGenerator random(53);
    int parsed = 0;
    for (int round = 0; round < 3000; ++round) {
        QByteArray bad = original;
        const int changes = 1 + static_cast<int>(random.bounded(4));
        for (int i = 0; i < changes; ++i) {
            const qsizetype at = random.bounded(static_cast<qint64>(bad.size()));
            bad[at] = static_cast<char>(random.bounded(256));
        }
        // 結果可以是成功（改到標籤文字）或不支援，只要不 crash
        if (parseFpl(bad)) {
            ++parsed;
        }
    }
    CHECK(parsed >= 0);
}
} // namespace

TEST_CASE("AC-13-3：空檔、標頭錯、截斷 → 不支援，結果畫面的訊息，不建立清單") {
    // 空檔
    CHECK(unsupported(QByteArray()));
    // 標頭錯（第一個與最後一個位元組）
    OneTrack sample = oneTrack();
    REQUIRE_FALSE(unsupported(sample.bytes));
    for (const qsizetype at : {qsizetype(0), qsizetype(15)}) {
        QByteArray bad = sample.bytes;
        bad[at] = static_cast<char>(bad[at] ^ 0x01);
        CHECK(unsupported(bad));
    }
    // 截斷：兩筆的樣本，每一種長度（含只剩標頭、切在字串表、meta、entries 中間）
    const QByteArray two = buildFpl(
        {{"file://C:\\a.flac", 0, "藝", "曲", "no"}, {"file://C:\\b.cue", 2, "", "", "", false}});
    REQUIRE_FALSE(unsupported(two));
    for (qsizetype length = 0; length < two.size(); ++length) {
        INFO(length);
        CHECK(unsupported(two.left(length)));
    }
    // 最後一筆後面多出位元組
    CHECK(unsupported(two + '\0'));
    // 經過匯入流程：空檔與壞檔都是 unsupported、沒有建立播放清單
    Fixture f;
    f.importer.importFiles({f.write(QStringLiteral("空.fpl"), QByteArray()),
                            f.write(QStringLiteral("截斷.fpl"), two.left(two.size() - 1))});
    QVariantMap result = f.importer.result();
    CHECK(result.value(QStringLiteral("kind")) == QStringLiteral("unsupported"));
    CHECK(result.value(QStringLiteral("message")) ==
          QStringLiteral("不支援此 .fpl 版本，請在 foobar2000 另存為 .m3u8。"));
    f.importer.dismiss();
    CHECK(f.importer.result().value(QStringLiteral("kind")) == QStringLiteral("unsupported"));
    f.importer.dismiss();
    CHECK(f.store.list().value().isEmpty());
}

TEST_CASE("AC-13-3：字串表長度、count、路徑位移越界與找不到 NUL → 不支援") {
    const OneTrack sample = oneTrack();
    const auto patched = [&](qsizetype at, quint32 value) {
        QByteArray bad = sample.bytes;
        setU32(bad, at, value);
        return bad;
    };
    // 字串表長度超過檔案
    CHECK(unsupported(patched(16, static_cast<quint32>(sample.bytes.size()))));
    CHECK(unsupported(patched(16, 0xFFFFFFFFU)));
    // count 超大（先和剩下的位元組數比，不配置）、比實際多一筆
    QElapsedTimer timer;
    timer.start();
    CHECK(unsupported(patched(sample.countAt, 0xFFFFFFFFU)));
    CHECK(unsupported(patched(sample.countAt, 0x7FFFFFFFU)));
    CHECK(timer.elapsed() < 1000);
    CHECK(unsupported(patched(sample.countAt, 2)));
    // 路徑位移在字串表外
    CHECK(unsupported(patched(sample.pathAt, static_cast<quint32>(sample.tableSize))));
    CHECK(unsupported(patched(sample.pathAt, 0xFFFFFFFFU)));
    // 字串表內找不到 NUL：只有一個字串的表，把結尾的 NUL 改掉
    OneTrack bare = oneTrack({"file://C:\\a.flac", 0, "", "", "", false});
    REQUIRE_FALSE(unsupported(bare.bytes));
    bare.bytes[20 + bare.tableSize - 1] = 'x';
    CHECK(unsupported(bare.bytes));
}

TEST_CASE("AC-13-3：entries 長度 E 不符、E 超大、pk／sk 乘法溢位、值與欄位位移越界 → 不支援") {
    const OneTrack sample = oneTrack(); // pk=2、sk=1、nvals=2、E=4+4+2+2=12
    REQUIRE(u32At(sample.bytes, sample.entryCountAt) == 12);
    const auto patched = [&](qsizetype at, quint32 value) {
        QByteArray bad = sample.bytes;
        setU32(bad, at, value);
        return bad;
    };
    const qsizetype e = sample.entriesAt;
    // E 不符：nvals 多一、sk 少一（E 照舊；總和比 E 大或小都不行）
    CHECK(unsupported(patched(e + 4 * 7, 3)));
    CHECK(unsupported(patched(e + 4, 0)));
    // E 超大、E 小於 4、E 比剩下的少一（最後一筆沒剛好到檔尾）
    CHECK(unsupported(patched(sample.entryCountAt, 0xFFFFFFFFU)));
    CHECK(unsupported(patched(sample.entryCountAt, 0x40000000U)));
    CHECK(unsupported(patched(sample.entryCountAt, 3)));
    CHECK(unsupported(patched(sample.entryCountAt, 11)));
    // pk 讓 3+2*pk 溢位或超過 E；sk 讓總和不符
    CHECK(unsupported(patched(e, 0x80000000U)));
    CHECK(unsupported(patched(e, 0x7FFFFFFFU)));
    CHECK(unsupported(patched(e, 0xFFFFFFFFU)));
    CHECK(unsupported(patched(e + 4, 0xFFFFFFFFU)));
    CHECK(unsupported(patched(e + 4, 0x80000000U)));
    // 標籤的值索引超過 nvals、順序倒過來
    CHECK(unsupported(patched(e + 4 * 3, 3)));
    CHECK(unsupported(patched(e + 4 * 5, 0xFFFFFFFFU)));
    {
        QByteArray bad = patched(e + 4 * 3, 1);
        setU32(bad, e + 4 * 5, 0);
        CHECK(unsupported(bad));
    }
    // 標籤名、值、次要欄位的字串位移越界
    const quint32 outside = static_cast<quint32>(sample.tableSize);
    CHECK(unsupported(patched(e + 4 * 4, outside)));
    CHECK(unsupported(patched(e + 4 * 8, outside)));
    CHECK(unsupported(patched(e + 4 * 10, outside)));
    CHECK(unsupported(patched(e + 4 * 11, 0xFFFFFFFFU)));
}

TEST_CASE("AC-13-3：大量引用長字串中間的位移 → 解碼總量有上限，很快回不支援") {
    // 1 MB 的字串表（一個長字串），兩萬筆的路徑位移都不同：不設上限會解碼約 20 GB
    QByteArray table(1024 * 1024, 'a');
    table.append('\0');
    QByteArray bytes = kFplHeader;
    appendU32(bytes, static_cast<quint32>(table.size()));
    bytes += table;
    constexpr quint32 count = 20000;
    appendU32(bytes, count);
    for (quint32 i = 0; i < count; ++i) {
        appendU32(bytes, 0x10);
        appendU32(bytes, i);
        appendU32(bytes, 0);
        appendU32(bytes, 4);
        for (const quint32 value : {0U, 0U, 0U, 0U}) {
            appendU32(bytes, value);
        }
    }
    QElapsedTimer timer;
    timer.start();
    CHECK(unsupported(bytes));
    CHECK(timer.elapsed() < 5000);
}

TEST_CASE(
    "AC-13-3（S-4）：結構合法但筆數大到記憶體峰值過高（超過 20 萬筆）→ 不支援；20 萬筆照常讀") {
    using auralis::mod::pl::kMaxFplEntries;
    REQUIRE(kMaxFplEntries == 200'000);
    // 一筆最少 32 bytes（沒有 meta、沒有標籤）：20 萬零一筆只要約 6 MB，遠小於讀取上限
    QList<auralis::test::FplTrack> tracks(kMaxFplEntries + 1,
                                          {"file://C:\\a.flac", 0, "", "", "", false});
    const QByteArray tooMany = buildFpl(tracks);
    CHECK(tooMany.size() < 8 * 1024 * 1024);
    CHECK(unsupported(tooMany));
    tracks.removeLast();
    const auto entries = parseFpl(buildFpl(tracks));
    REQUIRE(entries);
    CHECK(entries->size() == kMaxFplEntries);
}

TEST_CASE("AC-13-3：超過單檔讀取上限的 .fpl 是讀不到；隨機改壞的 test1.fpl 不 crash") {
    Fixture f;
    QByteArray big = oneTrack().bytes;
    big.append(auralis::mod::pl::kMaxFplBytes + 1 - big.size(), '\0');
    const QString path = f.write(QStringLiteral("太大.fpl"), big);
    CHECK_FALSE(readPlaylistFile(path));
    f.importer.importFiles({path});
    CHECK(f.importer.result().value(QStringLiteral("kind")) == QStringLiteral("unreadable"));
    f.importer.dismiss();
    CHECK(f.store.list().value().isEmpty());

    fuzzTest1(Samples::Public);
}

TEST_CASE("AC-13-3（私有真樣本）：隨機改壞的 test1.fpl 不 crash", "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    fuzzTest1(Samples::Private);
}

TEST_CASE("AC-13-3（M-04 審查）：大量項目共用超長多值標籤 → 每筆只複製額度內的字，很快讀完") {
    // 字串表：路徑、title、短值、4 MB 長值；每筆的 title 是「短值; 長值」
    // （沒有 artist 時 label＝title）。先串接再截短時每筆都複製整段長值（3,000 筆約 24 GB），要很久
    const QByteArray path = "file://C:\\a.flac";
    const QByteArray longValue(4 * 1024 * 1024, 'a');
    QByteArray table = path + '\0' + "title" + '\0' + "x" + '\0' + longValue + '\0';
    const auto pathAt = 0U;
    const auto keyAt = static_cast<quint32>(path.size() + 1);
    const auto shortAt = keyAt + 6;
    const auto longAt = shortAt + 2;
    QByteArray bytes = kFplHeader;
    appendU32(bytes, static_cast<quint32>(table.size()));
    bytes += table;
    constexpr quint32 count = 3000;
    appendU32(bytes, count);
    for (quint32 i = 0; i < count; ++i) {
        appendU32(bytes, 0x10);
        appendU32(bytes, pathAt);
        appendU32(bytes, 0);
        // [pk=1, sk=0, 0, begin=0, key, nvals=2, 短值, 長值]
        appendU32(bytes, 8);
        for (const quint32 value : {1U, 0U, 0U, 0U, keyAt, 2U, shortAt, longAt}) {
            appendU32(bytes, value);
        }
    }
    QElapsedTimer timer;
    timer.start();
    const auto entries = parseFpl(bytes);
    const qint64 elapsed = timer.elapsed();
    REQUIRE(entries);
    REQUIRE(entries->size() == count);
    CHECK(entries->first().label.size() == 1024);
    CHECK(entries->first().label.startsWith(QStringLiteral("x; aaa")));
    CAPTURE(elapsed);
    CHECK(elapsed < 2000);
}
