// m3u／m3u8／pls 裡的 .cue 展開成整張 CUE 專輯（T-56，US-12 AC-1、US-13 AC-2、AC-4）：
// 樣本 test3.m3u8（一行 .cue）配假音樂庫 → 和 test3.fpl 相同的 18 首；音樂庫沒有時 1 首、找不到；
// 掃描完成後的重新檢查展開；內嵌 cue 對不到的項目重新檢查以音樂庫為準
// （整軌檔在磁碟上也不清 missing）。G:、D: 的路徑用假的檔案查詢（不碰真的磁碟機）。
// 公開的合成樣本一定跑，私有真樣本不在時 SKIP（T-100）。
#include "pl_import_test_helpers.h"
#include "playlist_cue.h"
#include "playlist_parse.h"
#include "playlist_remap.h"

#include <QSignalSpy>

using auralis::pathKey;
using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::mod::pl::libraryCheck;
using auralis::mod::pl::readPlaylistFile;
using auralis::test::Samples;
using auralis::test::sha256;
using auralis::test::utf8;
using Fixture = auralis::test::ImportFixture;

namespace {

/// 音樂庫的替身：refs 是音樂庫裡的曲目；tracks() 記錄呼叫次數。
class CueLibrary final : public auralis::LibraryQuery {
public:
    QList<TrackRef> refs;
    mutable int tracksCalls = 0;
    auralis::Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                             quint32 subsong) const override {
        for (const TrackRef& ref : refs) {
            if (pathKey(ref.path) == pathKey(path) && ref.subsong == subsong) {
                TrackInfo info;
                info.ref = ref;
                return std::optional<TrackInfo>(info);
            }
        }
        return std::optional<TrackInfo>();
    }
    auralis::Result<QList<TrackInfo>> tracks(const auralis::TrackFilter&) const override {
        ++tracksCalls;
        QList<TrackInfo> result;
        for (const TrackRef& ref : refs) {
            TrackInfo info;
            info.ref = ref;
            result.append(info);
        }
        return result;
    }
    auralis::Result<QList<auralis::AlbumInfo>> albums() const override {
        return QList<auralis::AlbumInfo>();
    }
    auralis::Result<QStringList> artists() const override { return QStringList(); }
    auralis::Result<QList<auralis::FolderInfo>> folders(qint64) const override {
        return QList<auralis::FolderInfo>();
    }
    auralis::Result<QString> readEmbeddedLyrics(const TrackRef&) const override {
        return QString();
    }
    /// 一張 .cue 的 1..count 軌（故意倒過來放：展開要照軌號排序，不照 tracks() 的順序）。
    void addCue(const QString& cue, quint32 count) {
        for (quint32 track = count; track >= 1; --track) {
            refs.append({cue, track});
        }
    }
};

/// 有音樂庫查詢的匯入測試環境。
struct CueFixture : Fixture {
    CueLibrary library;
    CueFixture() {
        importer.setLibraryCheck([this] { return libraryCheck(library); });
    }
};

QList<TrackRef> refsOf(const Fixture& f, qint64 id) {
    QList<TrackRef> result;
    // Result 暫存物件的 value() 不能直接給 range-for（P-43）
    const auto items = f.store.items(id);
    for (const auto& item : items.value()) {
        result.append(item.track);
    }
    return result;
}

QList<bool> missingFlags(const Fixture& f, qint64 id) {
    QList<bool> flags;
    const auto items = f.store.items(id);
    for (const auto& item : items.value()) {
        flags.append(item.missing);
    }
    return flags;
}

QString test3Cue(Samples samples) {
    const auto m3u =
        readPlaylistFile(auralis::test::fixture(samples, QStringLiteral("test3.m3u8")));
    REQUIRE(m3u);
    REQUIRE(m3u.value().entries.size() == 1);
    return m3u.value().entries.first().path;
}

/// T-56 第 3 條：test3.m3u8＋音樂庫有 18 軌 → 和 test3.fpl 相同的 18 首。
void checkTest3Expanded(Samples samples) {
    const auto fixture = [samples](const QString& name) {
        return auralis::test::fixture(samples, name);
    };
    const QString cue = test3Cue(samples);
    CueFixture f;
    f.everythingExists = true;
    f.library.addCue(cue, 18);
    const QByteArray before = sha256(fixture(QStringLiteral("test3.m3u8")));
    f.importer.importFiles({fixture(QStringLiteral("test3.fpl"))});
    const qint64 fplId = f.lastId();
    f.importer.importFiles({fixture(QStringLiteral("test3.m3u8"))});
    const qint64 m3uId = f.lastId();
    REQUIRE(m3uId != fplId);
    const QList<TrackRef> m3u = refsOf(f, m3uId);
    const QList<TrackRef> fpl = refsOf(f, fplId);
    REQUIRE(m3u.size() == 18);
    REQUIRE(fpl.size() == 18);
    for (qsizetype i = 0; i < 18; ++i) {
        INFO(i);
        CHECK(m3u.at(i).path == fpl.at(i).path);
        CHECK(m3u.at(i).subsong == fpl.at(i).subsong);
        CHECK(m3u.at(i).subsong == static_cast<quint32>(i + 1));
    }
    CHECK(missingFlags(f, m3uId) == QList<bool>(18, false));
    CHECK(f.importer.result().isEmpty());
    CHECK(f.notifier.texts.last().endsWith(QStringLiteral("，共 18 首")));
    // 一次匯入只讀一次音樂庫；只讀不改（AC-12-4）
    CHECK(f.library.tracksCalls == 1);
    CHECK(sha256(fixture(QStringLiteral("test3.m3u8"))) == before);
}

/// T-56 第 3 條：test3.m3u8＋音樂庫沒有這個 .cue → 1 首、找不到。
void checkTest3Missing(Samples samples) {
    const auto fixture = [samples](const QString& name) {
        return auralis::test::fixture(samples, name);
    };
    const QString cue = test3Cue(samples);
    CueFixture f;
    f.everythingExists = true; // .cue 在磁碟上也一樣算找不到（不顯示成可播放的「不在音樂庫」）
    f.library.refs = {{QStringLiteral("D:\\別的\\別的.cue"), 1}};
    f.importer.importFiles({fixture(QStringLiteral("test3.m3u8"))});
    const qint64 id = f.lastId();
    REQUIRE(refsOf(f, id).size() == 1);
    CHECK(refsOf(f, id).first().path == cue);
    CHECK(refsOf(f, id).first().subsong == 0);
    CHECK(missingFlags(f, id) == QList<bool>{true});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("total")).toInt() == 1);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 1);
    const QVariantMap item = result.value(QStringLiteral("missingItems")).toList().first().toMap();
    CHECK(item.value(QStringLiteral("path")) == cue);
    CHECK(item.value(QStringLiteral("reason")) == QStringLiteral("音樂庫裡沒有這個 .cue 的曲目"));
    // 找不到的判斷不查磁碟
    CHECK_FALSE(f.queried.contains(cue));
}

} // namespace

TEST_CASE(
    "T-56 第 3 條：test3.m3u8＋音樂庫有 18 軌 → 18 首 (.cue, 1..18)，和 test3.fpl 匯入結果相同") {
    checkTest3Expanded(Samples::Public);
}

TEST_CASE("T-56 第 3 條（私有真樣本）：test3.m3u8＋音樂庫有 18 軌 → 和 test3.fpl 相同的 18 首",
          "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest3Expanded(Samples::Private);
}

TEST_CASE("T-56 第 3 條：test3.m3u8＋音樂庫沒有這個 .cue → 1 首 (.cue, 0)、找不到，原因寫音樂庫") {
    checkTest3Missing(Samples::Public);
}

TEST_CASE("T-56 第 3 條（私有真樣本）：test3.m3u8＋音樂庫沒有這個 .cue → 1 首、找不到",
          "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest3Missing(Samples::Private);
}

TEST_CASE("T-56 第 1 條：.cue 項目插在原本的位置展開，出現兩次就展開兩次；pls 也一樣；沒有 .cue "
          "時不查音樂庫") {
    CueFixture f;
    f.everythingExists = true;
    const QString cue = f.native(QStringLiteral("專輯/專輯.CUE"));
    // 音樂庫的路徑大小寫不同也對得上（pathKey）
    f.library.addCue(f.native(QStringLiteral("專輯/專輯.cue")), 3);
    const QString file = f.write(QStringLiteral("混合.m3u8"),
                                 utf8("#EXTM3U\na.flac\n專輯/專輯.CUE\nb.flac\n專輯\\專輯.CUE\n"));
    f.importer.importFiles({file});
    const QList<TrackRef> refs = refsOf(f, f.lastId());
    REQUIRE(refs.size() == 8);
    CHECK(refs.at(0).path == f.native(QStringLiteral("a.flac")));
    for (int i = 0; i < 3; ++i) {
        CHECK(refs.at(1 + i).path == cue);
        CHECK(refs.at(1 + i).subsong == static_cast<quint32>(i + 1));
        CHECK(refs.at(5 + i).path == cue);
        CHECK(refs.at(5 + i).subsong == static_cast<quint32>(i + 1));
    }
    CHECK(refs.at(4).path == f.native(QStringLiteral("b.flac")));
    CHECK(f.library.tracksCalls == 1);

    const QString pls = f.write(QStringLiteral("清單.pls"),
                                utf8("[playlist]\nFile2=b.flac\nFile1=專輯/專輯.CUE\n"));
    f.importer.importFiles({pls});
    const QList<TrackRef> fromPls = refsOf(f, f.lastId());
    REQUIRE(fromPls.size() == 4);
    CHECK(fromPls.at(2).subsong == 3);
    CHECK(fromPls.at(3).path == f.native(QStringLiteral("b.flac")));

    // 沒有 .cue 項目的清單不讀音樂庫
    CueFixture plain;
    plain.everythingExists = true;
    plain.importer.importFiles({plain.write(QStringLiteral("一般.m3u8"), utf8("a.flac\n"))});
    CHECK(plain.library.tracksCalls == 0);
}

TEST_CASE(
    "T-56 第 2 條：掃描完成後的重新檢查，音樂庫已有這個 .cue 的曲目時展開；還沒有時維持找不到") {
    CueFixture f;
    f.everythingExists = true;
    const QString cue = QStringLiteral("D:\\音樂\\專輯\\專輯.cue");
    const qint64 id = f.store
                          .createImported(QStringLiteral("清單"),
                                          {{0, {QStringLiteral("D:\\音樂\\a.flac"), 0}, false},
                                           {1, {cue, 0}, false}, // T-51 存的舊資料：沒標 missing
                                           {2, {QStringLiteral("D:\\音樂\\b.flac"), 0}, false}})
                          .value()
                          .id;
    QSignalSpy done(&f.importer, &auralis::mod::pl::PlaylistImporter::rechecked);
    f.importer.recheckAll();
    CHECK(missingFlags(f, id) == QList<bool>{false, true, false});

    f.library.addCue(cue, 2);
    f.importer.recheckAll();
    REQUIRE(done.count() == 2);
    CHECK(done.last().first().toInt() == 1);
    const QList<TrackRef> refs = refsOf(f, id);
    REQUIRE(refs.size() == 4);
    CHECK(refs.at(1).path == cue);
    CHECK(refs.at(1).subsong == 1);
    CHECK(refs.at(2).subsong == 2);
    CHECK(refs.at(3).path == QStringLiteral("D:\\音樂\\b.flac"));
    CHECK(missingFlags(f, id) == QList<bool>{false, false, false, false});
}

TEST_CASE("T-56 第 2 條：重新對應時整張 CUE 以音樂庫為準，找到就展開；S-09 的原因寫音樂庫") {
    CueFixture f;
    // 別張的 .cue 在新資料夾的磁碟上，但音樂庫沒有
    f.existing = {QStringLiteral("D:\\Music\\專輯\\專輯.cue"),
                  QStringLiteral("D:\\Music\\別張\\別張.cue"), QStringLiteral("D:\\Music\\b.flac")};
    f.library.addCue(QStringLiteral("D:\\Music\\專輯\\專輯.cue"), 2);
    const qint64 id =
        f.store
            .createImported(QStringLiteral("清單"),
                            {{0, {QStringLiteral("G:\\音樂\\專輯\\專輯.cue"), 0}, true},
                             {1, {QStringLiteral("G:\\音樂\\別張\\別張.cue"), 0}, true},
                             {2, {QStringLiteral("G:\\音樂\\b.flac"), 0}, true}})
            .value()
            .id;
    f.importer.openRemap(id);
    const QVariantList before = f.importer.result().value(QStringLiteral("missingItems")).toList();
    REQUIRE(before.size() == 3);
    CHECK(before.at(0).toMap().value(QStringLiteral("reason")) ==
          QStringLiteral("音樂庫裡沒有這個 .cue 的曲目"));
    CHECK(before.at(2).toMap().value(QStringLiteral("reason")) == QStringLiteral("找不到檔案"));

    f.importer.applyRemap(QStringLiteral("G:\\音樂"), QStringLiteral("D:\\Music"));
    const QList<TrackRef> refs = refsOf(f, id);
    REQUIRE(refs.size() == 4);
    CHECK(refs.at(0).path == QStringLiteral("D:\\Music\\專輯\\專輯.cue"));
    CHECK(refs.at(0).subsong == 1);
    CHECK(refs.at(1).subsong == 2);
    // 別張不在音樂庫：.cue 在磁碟上也維持原路徑與找不到
    CHECK(refs.at(2).path == QStringLiteral("G:\\音樂\\別張\\別張.cue"));
    CHECK(missingFlags(f, id) == QList<bool>{false, false, true, false});
    CHECK(f.importer.result().value(QStringLiteral("remapped")).toInt() == 2);
}

TEST_CASE("T-56 第 4 條：內嵌 cue "
          "對不到的項目（整軌檔＋軌號），重新檢查以音樂庫為準，整軌檔在磁碟上也不清") {
    CueFixture f;
    f.everythingExists = true;
    const QString image = QStringLiteral("D:\\音樂\\整軌.flac");
    const qint64 id = f.store
                          .createImported(QStringLiteral("清單"),
                                          {{0, {image, 3}, true},
                                           {1, {QStringLiteral("D:\\音樂\\a.flac"), 0}, true}})
                          .value()
                          .id;
    f.importer.recheckAll();
    CHECK(missingFlags(f, id) == QList<bool>{true, false});

    // 重新對應也一樣：整軌檔換到新資料夾而且在磁碟上，音樂庫沒有這個身分就維持找不到
    const qint64 moved =
        f.store
            .createImported(QStringLiteral("搬過"),
                            {{0, {QStringLiteral("G:\\音樂\\整軌.flac"), 3}, true}})
            .value()
            .id;
    REQUIRE(auralis::mod::pl::remapPlaylist(
                f.store, moved, QStringLiteral("G:\\音樂"), QStringLiteral("D:\\音樂"),
                [](const QString&, const QString&) { return true; }, libraryCheck(f.library))
                .hasValue());
    CHECK(missingFlags(f, moved) == QList<bool>{true});

    // 音樂庫有這個身分（核心之後支援內嵌 cuesheet 時）才清掉
    f.library.refs.append({image, 3});
    f.importer.recheckAll();
    CHECK(missingFlags(f, id) == QList<bool>{false, false});
}

TEST_CASE("AC-12-3（M-04 審查）：同一個檔的兩軌在檢查與寫入之間交換位置，過期的結果不寫到另一軌") {
    Fixture f;
    const QString image = QStringLiteral("G:\\音樂\\整軌.flac");
    const auto created = f.store.createImported(QStringLiteral("兩軌"),
                                                {{0, {image, 1}, true}, {1, {image, 2}, true}});
    REQUIRE(created.hasValue());
    const qint64 id = created.value().id;
    // 音樂庫只有新位置的第 1 軌；查第一次（pos 0 的第 1 軌）時另一邊把兩軌交換
    bool swapped = false;
    auralis::mod::pl::LibraryCheck library;
    library.contains = [&](const TrackRef& ref) {
        if (!swapped) {
            swapped = true;
            REQUIRE(f.store.reorder(id, {1, 0}).value());
        }
        return ref.subsong == 1 && ref.path.startsWith(QStringLiteral("D:"));
    };
    REQUIRE(auralis::mod::pl::remapPlaylist(
                f.store, id, QStringLiteral("G:\\音樂"), QStringLiteral("D:\\音樂"),
                [](const QString&, const QString&) { return false; }, library)
                .hasValue());
    // pos 0 現在是第 2 軌：不在音樂庫，不能被改成新路徑、清掉 missing
    const auto items = f.store.items(id).value();
    REQUIRE(items.size() == 2);
    CHECK(items.at(0).track.path == image);
    CHECK(items.at(0).track.subsong == 2);
    CHECK(items.at(0).missing);
    CHECK(items.at(1).track.path == image);
    CHECK(items.at(1).track.subsong == 1);
    CHECK(items.at(1).missing);
}
