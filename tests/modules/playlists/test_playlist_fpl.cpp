// foobar2000 .fpl 匯入（T-53，US-13 AC-1、2、4）：
// 回歸樣本 test1.fpl（和 test2.m3u8 同 40 首）、test3.fpl（獨立 .cue 的 18 軌）；
// 公開的合成樣本一定跑，私有真樣本不在時 SKIP（T-100）；
// subsong 只給 CUE 曲目；內嵌 cue 向音樂庫找；網路路徑拒絕不查詢。
// 壞檔（AC-13-3）在 test_playlist_fpl_bad.cpp。G: 的路徑用假的檔案查詢（不碰 G:）。
#include "pl_fpl_test_helpers.h"
#include "pl_import_test_helpers.h"
#include "playlist_fpl.h"
#include "playlist_parse.h"

#include <QHash>

using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::mod::pl::CueLookup;
using auralis::mod::pl::ImportEntry;
using auralis::mod::pl::libraryCueLookup;
using auralis::mod::pl::matchEmbeddedCue;
using auralis::mod::pl::parseFpl;
using auralis::mod::pl::readPlaylistFile;
using auralis::test::buildFpl;
using auralis::test::Samples;
using auralis::test::sha256;
using Fixture = auralis::test::ImportFixture;

namespace {

/// 音樂庫的替身：tracks() 與 audioFilePath() 記錄呼叫次數。
class CueLibrary final : public auralis::test::RefsLibrary {
public:
    QHash<QString, QString> audioOf; ///< .cue 路徑 → 整軌檔
    mutable QStringList audioCalls;
    auralis::Result<QString> audioFilePath(const TrackRef& ref) const override {
        audioCalls.append(ref.path);
        if (!audioOf.contains(ref.path)) {
            return auralis::Error{QStringLiteral("沒有"), 0, QStringLiteral("audioFilePath")};
        }
        return audioOf.value(ref.path);
    }
};

/// AC-13-1：test1.fpl（40 首，含 DSD 的 subsong 1、多值標籤）和 test2.m3u8 同一份清單。
void checkTest1(Samples samples) {
    const auto fixture = [samples](const QString& name) {
        return auralis::test::fixture(samples, name);
    };
    const auto fpl = readPlaylistFile(fixture(QStringLiteral("test1.fpl")));
    const auto m3u = readPlaylistFile(fixture(QStringLiteral("test2.m3u8")));
    REQUIRE(fpl);
    REQUIRE(m3u);
    REQUIRE_FALSE(fpl.value().unsupported);
    REQUIRE(fpl.value().entries.size() == 40);
    REQUIRE(m3u.value().entries.size() == 40);
    for (qsizetype i = 0; i < 40; ++i) {
        const ImportEntry& a = fpl.value().entries.at(i);
        const ImportEntry& b = m3u.value().entries.at(i);
        INFO(i);
        CHECK(a.path == b.path);
        CHECK(a.raw == b.raw);
        CHECK(a.rejected.isEmpty());
        CHECK(a.subsong == 0);
        CHECK_FALSE(a.embeddedCue);
        CHECK_FALSE(a.label.isEmpty());
    }
    CHECK(fpl.value().name == QStringLiteral("test1"));

    // 經過匯入流程：和 test2.m3u8 匯入的清單完全相同；只讀不改（AC-12-4）
    Fixture f;
    f.everythingExists = true;
    const QByteArray before = sha256(fixture(QStringLiteral("test1.fpl")));
    f.importer.importFiles({fixture(QStringLiteral("test2.m3u8"))});
    const qint64 m3uId = f.lastId();
    f.importer.importFiles({fixture(QStringLiteral("test1.fpl"))});
    const qint64 fplId = f.lastId();
    REQUIRE(fplId != m3uId);
    const auto m3uItems = f.store.items(m3uId).value();
    const auto fplItems = f.store.items(fplId).value();
    REQUIRE(fplItems.size() == 40);
    for (qsizetype i = 0; i < 40; ++i) {
        CHECK(fplItems.at(i).track.path == m3uItems.at(i).track.path);
        CHECK(fplItems.at(i).track.subsong == 0);
    }
    CHECK(f.notifier.texts.last() == QStringLiteral("已匯入「test1」，共 40 首"));
    CHECK(sha256(fixture(QStringLiteral("test1.fpl"))) == before);
}

/// AC-13-2：test3.fpl（獨立 .cue 的 18 軌）。firstLabel 空的＝只檢查「藝人 - 曲名」的形式
/// （真樣本的曲名不寫進公開的測試原始碼）。
void checkTest3(Samples samples, const QString& firstLabel) {
    const auto fixture = [samples](const QString& name) {
        return auralis::test::fixture(samples, name);
    };
    // test3.m3u8 是 foobar2000 對同一份清單匯出的 m3u8：只有一行（.cue 本身）
    const auto m3u = readPlaylistFile(fixture(QStringLiteral("test3.m3u8")));
    REQUIRE(m3u);
    REQUIRE(m3u.value().entries.size() == 1);
    const QString cue = m3u.value().entries.first().path;
    REQUIRE(cue.endsWith(QStringLiteral(".cue")));

    const auto fpl = readPlaylistFile(fixture(QStringLiteral("test3.fpl")));
    REQUIRE(fpl);
    REQUIRE_FALSE(fpl.value().unsupported);
    REQUIRE(fpl.value().entries.size() == 18);
    for (qsizetype i = 0; i < 18; ++i) {
        const ImportEntry& entry = fpl.value().entries.at(i);
        INFO(i);
        CHECK(entry.path == cue);
        CHECK(entry.subsong == static_cast<quint32>(i + 1));
        CHECK_FALSE(entry.embeddedCue); // cue_embedded=no
    }
    if (firstLabel.isEmpty()) {
        CHECK(fpl.value().entries.first().label.contains(QStringLiteral(" - ")));
    } else {
        CHECK(fpl.value().entries.first().label == firstLabel);
    }

    // 經過匯入流程：存的身分就是核心 CUE 曲目的身分 (.cue 路徑, 軌號)
    Fixture f;
    f.everythingExists = true;
    f.importer.importFiles({fixture(QStringLiteral("test3.fpl"))});
    const auto items = f.store.items(f.lastId()).value();
    REQUIRE(items.size() == 18);
    for (qsizetype i = 0; i < 18; ++i) {
        CHECK(items.at(i).track.path == cue);
        CHECK(items.at(i).track.subsong == static_cast<quint32>(i + 1));
        CHECK_FALSE(items.at(i).missing);
    }
    // 查檔案系統的只有 .cue 本身
    for (const QString& path : f.queried) {
        CHECK(path == cue);
    }
}

} // namespace

TEST_CASE("AC-13-1：test1.fpl 匯入成 40 首，路徑與順序和 test2.m3u8 相同，subsong 都是 0") {
    checkTest1(Samples::Public);
}

TEST_CASE("AC-13-1（私有真樣本）：test1.fpl 匯入成 40 首，路徑與順序和 test2.m3u8 相同",
          "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest1(Samples::Private);
}

TEST_CASE("AC-13-2：test3.fpl（獨立 .cue）匯入成 18 首 (.cue, 1..18)，順序相同") {
    checkTest3(Samples::Public, QStringLiteral("藝人 74 - 曲名 35"));
}

TEST_CASE("AC-13-2（私有真樣本）：test3.fpl（獨立 .cue）匯入成 18 首 (.cue, 1..18)",
          "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest3(Samples::Private, QString());
}

TEST_CASE("AC-13-4：找不到的清單顯示「藝人 - 曲名」，沒有標籤時顯示路徑") {
    Fixture f;
    const QString file = f.write(QStringLiteral("標籤.fpl"),
                                 buildFpl({{"file://C:\\音樂\\a.flac", 0, "藝人", "曲名", "no"},
                                           {"file://C:\\音樂\\b.flac", 0, "", "只有曲名", ""},
                                           {"file://C:\\音樂\\c.flac", 0, "", "", "", false}}));
    f.importer.importFiles({file});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    const QVariantList missing = result.value(QStringLiteral("missingItems")).toList();
    REQUIRE(missing.size() == 3);
    CHECK(missing.at(0).toMap().value(QStringLiteral("path")) == QStringLiteral("藝人 - 曲名"));
    CHECK(missing.at(1).toMap().value(QStringLiteral("path")) == QStringLiteral("只有曲名"));
    CHECK(missing.at(2).toMap().value(QStringLiteral("path")) ==
          QStringLiteral("C:\\音樂\\c.flac"));
    CHECK(missing.at(0).toMap().value(QStringLiteral("reason")) == QStringLiteral("找不到檔案"));
    // 標籤只用來顯示：清單存的是路徑
    const auto items = f.store.items(result.value(QStringLiteral("id")).toLongLong()).value();
    CHECK(items.at(0).track.path == QStringLiteral("C:\\音樂\\a.flac"));
}

TEST_CASE("subsong：非 CUE 的檔案忽略（DSD 的 1 不是第 1 軌）；.cue 或 cue_embedded=yes 才用") {
    const auto entries = parseFpl(buildFpl({
        {"file://C:\\音樂\\a.dsf", 1, "", "", "no"},
        {"file://C:\\音樂\\b.flac", 2, "", "", ""},
        {"file://C:\\音樂\\c.flac", 3, "", "", "no"},
        {"file://C:\\音樂\\D.CUE", 5, "", "", ""},
        {"file://C:\\音樂\\e.flac", 4, "", "", "yes"},
        {"file://C:\\音樂\\f.flac", 0, "", "", "yes"},
    }));
    REQUIRE(entries);
    REQUIRE(entries->size() == 6);
    CHECK(entries->at(0).subsong == 0);
    CHECK(entries->at(1).subsong == 0);
    CHECK(entries->at(2).subsong == 0);
    CHECK(entries->at(3).subsong == 5);
    CHECK(entries->at(3).path == QStringLiteral("C:\\音樂\\D.CUE"));
    CHECK_FALSE(entries->at(3).embeddedCue);
    CHECK(entries->at(4).subsong == 4);
    CHECK(entries->at(4).embeddedCue);
    // cue_embedded=yes 但 subsong 0＝整個檔案
    CHECK(entries->at(5).subsong == 0);
    CHECK_FALSE(entries->at(5).embeddedCue);
    for (const ImportEntry& entry : *entries) {
        CHECK(entry.rejected.isEmpty());
    }
}

TEST_CASE(
    "AC-13-2：內嵌 cue（產生的樣本）向音樂庫換成 CUE 曲目；找不到時存原路徑＋軌號並標找不到") {
    Fixture f;
    f.everythingExists = true;
    const QString file = f.write(QStringLiteral("內嵌.fpl"),
                                 buildFpl({{"file://C:\\音樂\\整軌.flac", 3, "", "", "yes"},
                                           {"file://C:\\音樂\\整軌.flac", 9, "藝", "九", "yes"}}));
    QList<QPair<QString, quint32>> asked;
    f.importer.setCueLookup([&asked] {
        return CueLookup([&asked](const QString& audio, quint32 track) -> std::optional<TrackRef> {
            asked.append({audio, track});
            if (track == 3) {
                return TrackRef{QStringLiteral("C:\\音樂\\整軌.cue"), 3};
            }
            return std::nullopt;
        });
    });
    f.importer.importFiles({file});
    CHECK(asked == QList<QPair<QString, quint32>>{{QStringLiteral("C:\\音樂\\整軌.flac"), 3},
                                                  {QStringLiteral("C:\\音樂\\整軌.flac"), 9}});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    const auto items = f.store.items(result.value(QStringLiteral("id")).toLongLong()).value();
    REQUIRE(items.size() == 2);
    CHECK(items.at(0).track.path == QStringLiteral("C:\\音樂\\整軌.cue"));
    CHECK(items.at(0).track.subsong == 3);
    CHECK_FALSE(items.at(0).missing);
    CHECK(items.at(1).track.path == QStringLiteral("C:\\音樂\\整軌.flac"));
    CHECK(items.at(1).track.subsong == 9);
    CHECK(items.at(1).missing);
    // 找不到的那一筆不查檔案系統（整軌檔存在也不算找到）
    CHECK(f.queried == QStringList{QStringLiteral("C:\\音樂\\整軌.cue")});
    const QVariantMap missing =
        result.value(QStringLiteral("missingItems")).toList().first().toMap();
    CHECK(missing.value(QStringLiteral("path")) == QStringLiteral("藝 - 九"));
    CHECK(missing.value(QStringLiteral("reason")) ==
          QStringLiteral("音樂庫裡沒有這一軌的 CUE 曲目"));

    // 沒有設 lookup：內嵌 cue 一律找不到
    QList<ImportEntry> entries = *parseFpl(buildFpl({{"file://C:\\a.flac", 2, "", "", "yes"}}));
    matchEmbeddedCue(entries, CueLookup());
    CHECK_FALSE(entries.first().unmatched.isEmpty());
}

TEST_CASE(
    "內嵌 cue 的音樂庫查詢：只讀一次曲目，軌號相同、整軌檔在 .cue 資料夾底下才問 audioFilePath") {
    CueLibrary library;
    library.refs = {{QStringLiteral("C:\\a\\x.cue"), 1},
                    {QStringLiteral("C:\\a\\x.cue"), 3},
                    {QStringLiteral("C:\\b\\y.cue"), 3},
                    {QStringLiteral("C:\\a\\x.flac"), 0},
                    {QStringLiteral("C:\\a\\z.CUE"), 3}};
    library.audioOf = {{QStringLiteral("C:\\a\\x.cue"), QStringLiteral("C:\\a\\x.flac")},
                       {QStringLiteral("C:\\b\\y.cue"), QStringLiteral("C:\\b\\y.flac")},
                       {QStringLiteral("C:\\a\\z.CUE"), QStringLiteral("C:\\a\\z.flac")}};
    const CueLookup lookup = libraryCueLookup(library);
    // 大小寫與分隔符不同也對得上（pathKey）
    const auto hit = lookup(QStringLiteral("c:/A/X.FLAC"), 3);
    REQUIRE(hit);
    CHECK(hit->path == QStringLiteral("C:\\a\\x.cue"));
    CHECK(hit->subsong == 3);
    // y.cue 在別的資料夾，不問
    CHECK_FALSE(library.audioCalls.contains(QStringLiteral("C:\\b\\y.cue")));
    CHECK_FALSE(lookup(QStringLiteral("C:\\a\\x.flac"), 2));
    CHECK_FALSE(lookup(QStringLiteral("C:\\a\\沒有.flac"), 3));
    const auto z = lookup(QStringLiteral("C:\\a\\z.flac"), 3);
    REQUIRE(z);
    CHECK(z->path == QStringLiteral("C:\\a\\z.CUE"));
    CHECK(library.tracksCalls == 1);
}

TEST_CASE("安全底線：.fpl 裡的網路路徑與其他協定被拒絕、照原文（去掉 file://）存，而且不查詢") {
    Fixture f;
    const QString file =
        f.write(QStringLiteral("網路.fpl"), buildFpl({{"file://\\\\evil-host\\share\\a.flac"},
                                                      {"http://evil-host/b.mp3"},
                                                      {"file://C:\\x\\NUL"},
                                                      {"相對\\c.flac"},
                                                      {"file://C:\\音樂\\本機.flac"}}));
    f.existing = {QStringLiteral("C:\\音樂\\本機.flac")};
    f.importer.importFiles({file});
    CHECK(f.queried == QStringList{QStringLiteral("C:\\音樂\\本機.flac")});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("missing")).toInt() == 4);
    const QVariantList missing = result.value(QStringLiteral("missingItems")).toList();
    CHECK(missing.first().toMap().value(QStringLiteral("reason")) ==
          QStringLiteral("網路上的路徑，不會開啟"));
    const auto items = f.store.items(result.value(QStringLiteral("id")).toLongLong()).value();
    CHECK(items.first().track.path == QStringLiteral("\\\\evil-host\\share\\a.flac"));
    CHECK(items.first().missing);
    CHECK(auralis::mod::pl::storedVerbatim(items.first().track.path, true));
}
