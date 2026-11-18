// 找不到的檔案與重新對應（T-52，US-12 AC-3、US-13 AC-4；S-08、S-09）：
// 共同資料夾的預填、「原本的資料夾 → 現在的資料夾」（大小寫與分隔符不同也對得上）、套用後重新檢查、
// 對應規則存進 path_remaps 並在下次自動建議；被拒絕的項目對應成本機路徑後放行（D-PL-07）；
// CUE 曲目；掃描完成後重新檢查所有播放清單（情況 2）；不在音樂庫（情況 3）；只改資料庫（AC-12-4）。
// G:、D: 的路徑都用假的檔案查詢（不碰真的磁碟機）。
#include "pl_import_test_helpers.h"
#include "playlist_remap.h"
#include "playlist_tracks.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>

using auralis::TrackRef;
using auralis::mod::pl::commonFolder;
using auralis::mod::pl::PlaylistItem;
using auralis::mod::pl::PlaylistTracks;
using auralis::mod::pl::remapPath;
using auralis::mod::pl::storedVerbatim;
using auralis::test::utf8;
using Fixture = auralis::test::ImportFixture;

namespace {

/// 直接在資料庫建一個播放清單（不經匯入）；回傳 id。
qint64 makeList(Fixture& f, const QList<PlaylistItem>& items) {
    const auto created = f.store.createImported(QStringLiteral("清單"), items);
    REQUIRE(created.hasValue());
    return created.value().id;
}

QList<bool> missingFlags(const Fixture& f, qint64 id) {
    QList<bool> flags;
    // Result 暫存物件的 value() 不能直接給 range-for（P-43）
    const auto items = f.store.items(id);
    for (const auto& item : items.value()) {
        flags.append(item.missing);
    }
    return flags;
}

} // namespace

TEST_CASE("AC-12-3：原本的資料夾預填找不到的路徑的共同資料夾（大小寫與分隔符不同也算同一層）") {
    CHECK(commonFolder({QStringLiteral("G:\\音樂\\A\\1.flac"), QStringLiteral("g:/音樂/b/2.flac"),
                        QStringLiteral("G:\\音樂\\a\\3.flac")}) == QStringLiteral("G:\\音樂"));
    CHECK(commonFolder({QStringLiteral("G:\\音樂\\A\\1.flac")}) == QStringLiteral("G:\\音樂\\A"));
    CHECK(commonFolder({QStringLiteral("G:\\a.flac"), QStringLiteral("G:\\b.flac")}) ==
          QStringLiteral("G:\\"));
    // 網路路徑和本機路徑沒有共同的一層
    CHECK(commonFolder({QStringLiteral("\\\\NAS\\音樂\\a.flac"), QStringLiteral("C:\\NAS\\b.flac")})
              .isEmpty());
    CHECK(commonFolder({QStringLiteral("\\\\NAS\\音樂\\a.flac"),
                        QStringLiteral("\\\\nas\\音樂\\x\\b.flac")}) ==
          QStringLiteral("\\\\NAS\\音樂"));
    CHECK(commonFolder({}).isEmpty());
}

TEST_CASE("AC-12-3：套用對應規則用 pathKey 一層一層比；跳出現在的資料夾或對到網路路徑時拒絕") {
    // 例：G:\音樂 → D:\Music，大小寫與分隔符不同也對得上
    const auto mapped = remapPath(QStringLiteral("g:/音樂/專輯/01.flac"),
                                  QStringLiteral("G:\\音樂\\"), QStringLiteral("D:/Music"));
    REQUIRE(mapped);
    REQUIRE(mapped->hasValue());
    CHECK(mapped->value() == QStringLiteral("D:\\Music\\專輯\\01.flac"));
    // 只比整層：G:\音樂2 不在 G:\音樂 底下；資料夾本身也不算
    CHECK_FALSE(remapPath(QStringLiteral("G:\\音樂2\\a.flac"), QStringLiteral("G:\\音樂"),
                          QStringLiteral("D:\\Music")));
    CHECK_FALSE(remapPath(QStringLiteral("G:\\音樂"), QStringLiteral("G:\\音樂"),
                          QStringLiteral("D:\\Music")));
    // 被拒絕的原文可能寫了 ..：跳出現在的資料夾
    const auto escape = remapPath(QStringLiteral("\\\\NAS\\音樂\\..\\..\\Windows\\x.flac"),
                                  QStringLiteral("\\\\NAS\\音樂"), QStringLiteral("D:\\Music"));
    REQUIRE(escape);
    CHECK_FALSE(escape->hasValue());
    // 現在的資料夾是網路路徑、或以下各層是裝置名稱：照路徑規則拒絕（不查詢、維持找不到）
    const auto network = remapPath(QStringLiteral("G:\\音樂\\a.flac"), QStringLiteral("G:\\音樂"),
                                   QStringLiteral("\\\\NAS\\音樂"));
    REQUIRE(network);
    CHECK_FALSE(network->hasValue());
    const auto device = remapPath(QStringLiteral("G:\\音樂\\NUL"), QStringLiteral("G:\\音樂"),
                                  QStringLiteral("D:\\Music"));
    REQUIRE(device);
    CHECK_FALSE(device->hasValue());
}

TEST_CASE(
    "AC-12-3：匯入後重新對應 → 更新數字與清單、全部找到；規則存進 path_remaps、下次自動建議") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\Music\\A\\1.flac"), QStringLiteral("D:\\Music\\b\\2.flac")};
    const QString list =
        f.write(QStringLiteral("舊.m3u8"), utf8("G:\\音樂\\A\\1.flac\nG:\\音樂\\b\\2.flac\n"));
    const QByteArray before = auralis::test::sha256(list);
    f.importer.importFiles({list});
    auto result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("mode")) == QStringLiteral("import"));
    CHECK(result.value(QStringLiteral("missing")).toInt() == 2);
    CHECK(result.value(QStringLiteral("oldRoot")).toString() == QStringLiteral("G:\\音樂"));
    CHECK(result.value(QStringLiteral("newRoot")).toString().isEmpty());
    const qint64 id = result.value(QStringLiteral("id")).toLongLong();

    QSignalSpy changed(&f.controller, &auralis::mod::pl::PlaylistsController::itemsChanged);
    f.queried.clear();
    f.starts.clear();
    f.importer.applyRemap(QStringLiteral("g:/音樂"), QStringLiteral("D:\\Music"));
    result = f.importer.result();
    CHECK(result.value(QStringLiteral("total")).toInt() == 2);
    CHECK(result.value(QStringLiteral("found")).toInt() == 2);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 0);
    CHECK(result.value(QStringLiteral("missingItems")).toList().isEmpty());
    CHECK(result.value(QStringLiteral("remapped")).toInt() == 2);
    CHECK(f.paths(id) == QStringList{QStringLiteral("D:\\Music\\A\\1.flac"),
                                     QStringLiteral("D:\\Music\\b\\2.flac")});
    CHECK(missingFlags(f, id) == QList<bool>{false, false});
    // 現在的資料夾是使用者給的起點：以它為起點查
    CHECK(f.starts == QStringList{QStringLiteral("D:\\Music"), QStringLiteral("D:\\Music")});
    CHECK(changed.count() == 1);
    // AC-12-4：只改資料庫，播放清單檔不變
    CHECK(auralis::test::sha256(list) == before);

    const auto remaps = f.store.remaps();
    REQUIRE(remaps.value().size() == 1);
    CHECK(remaps.value().first().oldRoot == QStringLiteral("g:\\音樂"));
    CHECK(remaps.value().first().newRoot == QStringLiteral("D:\\Music"));

    // 下次匯入同一個舊根目錄底下的清單：自動帶入建議
    f.importer.dismiss();
    f.existing.clear();
    f.importer.importFiles({f.write(QStringLiteral("又一個.m3u8"), utf8("G:\\音樂\\c\\3.flac\n"))});
    result = f.importer.result();
    CHECK(result.value(QStringLiteral("oldRoot")).toString() == QStringLiteral("g:\\音樂"));
    CHECK(result.value(QStringLiteral("newRoot")).toString() == QStringLiteral("D:\\Music"));
}

TEST_CASE("AC-12-3：對應後仍找不到的維持原路徑與 missing，規則不存；部分找到時數字照實更新") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\Music\\1.flac")};
    f.importer.importFiles(
        {f.write(QStringLiteral("半.m3u8"),
                 utf8("G:\\音樂\\1.flac\nG:\\音樂\\2.flac\nE:\\別處\\3.flac\n"))});
    const qint64 id = f.importer.result().value(QStringLiteral("id")).toLongLong();
    f.importer.applyRemap(QStringLiteral("G:\\音樂"), QStringLiteral("D:\\Music"));
    const auto result = f.importer.result();
    CHECK(result.value(QStringLiteral("found")).toInt() == 1);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 2);
    CHECK(result.value(QStringLiteral("missingItems")).toList().size() == 2);
    CHECK(f.paths(id) == QStringList{QStringLiteral("D:\\Music\\1.flac"),
                                     QStringLiteral("G:\\音樂\\2.flac"),
                                     QStringLiteral("E:\\別處\\3.flac")});
    CHECK(missingFlags(f, id) == QList<bool>{false, true, true});
    // 使用者輸入的兩個欄位留著，可以再改一次
    CHECK(result.value(QStringLiteral("oldRoot")).toString() == QStringLiteral("G:\\音樂"));

    // 全部對不到：規則不存
    Fixture g;
    g.importer.importFiles({g.write(QStringLiteral("無.m3u8"), utf8("G:\\音樂\\9.flac\n"))});
    g.importer.applyRemap(QStringLiteral("G:\\音樂"), QStringLiteral("D:\\Music"));
    CHECK(g.importer.result().value(QStringLiteral("missing")).toInt() == 1);
    CHECK(g.store.remaps().value().isEmpty());
}

TEST_CASE("D-PL-07：被拒絕的網路路徑對應成本機路徑後清掉 "
          "missing、可以播放；對應到網路路徑的維持且不查詢") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\音樂\\a.flac")};
    f.importer.importFiles({f.write(
        QStringLiteral("nas.m3u8"),
        utf8("\\\\NAS\\音樂\\a.flac\n\\\\NAS\\音樂\\b.flac\n\\\\evil-host\\share\\c.flac\n"))});
    const qint64 id = f.importer.result().value(QStringLiteral("id")).toLongLong();
    REQUIRE(id > 0);
    REQUIRE(storedVerbatim(f.paths(id).first(), true));

    // 對應到另一個網路路徑：照路徑規則拒絕，什麼都不查
    f.queried.clear();
    f.starts.clear();
    f.importer.applyRemap(QStringLiteral("\\\\NAS\\音樂"), QStringLiteral("\\\\other\\share"));
    CHECK(f.queried.isEmpty());
    CHECK(missingFlags(f, id) == QList<bool>{true, true, true});

    // 對應到本機：找到的清掉 missing（四個出口就放行），沒找到的與對不上的維持；網路路徑沒被查詢
    f.importer.applyRemap(QStringLiteral("\\\\nas\\音樂"), QStringLiteral("D:\\音樂"));
    CHECK(f.paths(id) == QStringList{QStringLiteral("D:\\音樂\\a.flac"),
                                     QStringLiteral("\\\\NAS\\音樂\\b.flac"),
                                     QStringLiteral("\\\\evil-host\\share\\c.flac")});
    CHECK(missingFlags(f, id) == QList<bool>{false, true, true});
    for (const QString& path : f.queried) {
        CHECK_FALSE(path.startsWith(QLatin1String("\\\\")));
    }
    CHECK_FALSE(storedVerbatim(f.paths(id).at(0), false));
    REQUIRE(f.controller.play(id));
    CHECK(f.playback.paths() == QStringList{QStringLiteral("D:\\音樂\\a.flac")});
}

TEST_CASE("第 6 條：CUE 曲目（.cue 路徑＋軌號）也能重新對應，軌號不變；一般曲目同一次對應") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\Music\\專輯\\專輯.cue"), QStringLiteral("D:\\Music\\b.flac")};
    const qint64 id = makeList(f, {{0, {QStringLiteral("G:\\音樂\\專輯\\專輯.cue"), 3}, true},
                                   {1, {QStringLiteral("G:\\音樂\\b.flac"), 0}, true}});
    f.importer.openRemap(id);
    REQUIRE(f.importer.result().value(QStringLiteral("mode")) == QStringLiteral("playlist"));
    f.importer.applyRemap(QStringLiteral("G:\\音樂"), QStringLiteral("D:\\Music"));
    const auto items = f.store.items(id).value();
    REQUIRE(items.size() == 2);
    CHECK(items.at(0).track.path == QStringLiteral("D:\\Music\\專輯\\專輯.cue"));
    CHECK(items.at(0).track.subsong == 3);
    CHECK_FALSE(items.at(0).missing);
    CHECK(items.at(1).track.path == QStringLiteral("D:\\Music\\b.flac"));
    CHECK_FALSE(items.at(1).missing);
}

TEST_CASE("S-08：「重新對應資料夾」先照磁碟重新檢查整份清單再開對話框；有別的結果時不開") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\在\\a.flac")};
    const qint64 id = makeList(f, {{0, {QStringLiteral("D:\\在\\a.flac"), 0}, true},
                                   {1, {QStringLiteral("D:\\不在\\b.flac"), 0}, false},
                                   {2, {QStringLiteral("\\\\NAS\\x\\c.flac"), 0}, true}});
    f.queried.clear();
    f.starts.clear();
    f.importer.openRemap(id);
    const auto result = f.importer.result();
    CHECK(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("name")) == QStringLiteral("清單"));
    CHECK(result.value(QStringLiteral("total")).toInt() == 3);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 2);
    CHECK(missingFlags(f, id) == QList<bool>{false, true, true});
    // 網路路徑不查詢；原因照路徑規則
    CHECK_FALSE(f.queried.contains(QStringLiteral("\\\\NAS\\x\\c.flac")));
    const auto items = result.value(QStringLiteral("missingItems")).toList();
    CHECK(items.at(1).toMap().value(QStringLiteral("reason")) ==
          QStringLiteral("網路上的路徑，不會開啟"));
    CHECK(items.at(0).toMap().value(QStringLiteral("reason")) == QStringLiteral("找不到檔案"));
    // 結果開著時再按一次不換掉它
    const qint64 other = makeList(f, {});
    f.importer.openRemap(other);
    CHECK(f.importer.result().value(QStringLiteral("id")).toLongLong() == id);
}

TEST_CASE(
    "情況 2：掃描完成後重新檢查所有播放清單的 missing（網路路徑不查詢、對應過的以當時的起點查）") {
    Fixture f;
    f.existing = {QStringLiteral("D:\\後來有了.flac"), QStringLiteral("D:\\Music\\對應過.flac")};
    REQUIRE(f.store.saveRemap({QStringLiteral("G:\\音樂"), QStringLiteral("D:\\Music")}));
    const qint64 first = makeList(f, {{0, {QStringLiteral("D:\\後來有了.flac"), 0}, true},
                                      {1, {QStringLiteral("D:\\被刪了.flac"), 0}, false}});
    const qint64 second = makeList(f, {{0, {QStringLiteral("\\\\NAS\\x.flac"), 0}, true},
                                       {1, {QStringLiteral("D:\\Music\\對應過.flac"), 0}, false}});
    QSignalSpy changed(&f.controller, &auralis::mod::pl::PlaylistsController::itemsChanged);
    QSignalSpy done(&f.importer, &auralis::mod::pl::PlaylistImporter::rechecked);
    f.queried.clear();
    f.starts.clear();
    f.importer.recheckAll();
    REQUIRE(done.count() == 1);
    CHECK(done.first().first().toInt() == 1);
    CHECK(missingFlags(f, first) == QList<bool>{false, true});
    CHECK(missingFlags(f, second) == QList<bool>{true, false});
    CHECK(changed.count() == 1);
    CHECK(changed.first().first().toLongLong() == first);
    CHECK_FALSE(f.queried.contains(QStringLiteral("\\\\NAS\\x.flac")));
    const qsizetype mapped = f.queried.indexOf(QStringLiteral("D:\\Music\\對應過.flac"));
    REQUIRE(mapped >= 0);
    CHECK(f.starts.at(mapped) == QStringLiteral("D:\\Music"));
}

TEST_CASE("情況 3：路徑在磁碟上但不在音樂庫標「不在音樂庫」、仍可播放；找不到的另外算") {
    Fixture f;
    const QString onDisk = f.write(QStringLiteral("沒加入的資料夾/a.flac"), "x");
    const qint64 id = makeList(f, {{0, {onDisk, 0}, false},
                                   {1, {f.native(QStringLiteral("不存在.flac")), 0}, false},
                                   {2, {QStringLiteral("\\\\NAS\\x.flac"), 0}, true}});
    PlaylistTracks tracks(f.store, f.controller, nullptr, &f.playback);
    tracks.setPlaylistId(id);
    QElapsedTimer timer;
    timer.start();
    while (tracks.status() != QStringLiteral("ready") && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    REQUIRE(tracks.count() == 3);
    const auto role = [&](int row, int r) {
        return tracks.data(tracks.index(row), r).toBool();
    };
    CHECK(role(0, PlaylistTracks::NotInLibraryRole));
    CHECK_FALSE(role(0, PlaylistTracks::MissingRole));
    CHECK_FALSE(role(1, PlaylistTracks::NotInLibraryRole));
    CHECK(role(1, PlaylistTracks::MissingRole));
    CHECK(role(2, PlaylistTracks::MissingRole));
    CHECK(tracks.missingCount() == 2);
    REQUIRE(tracks.playFrom(0));
    CHECK(f.playback.paths().first() == onDisk);
}

TEST_CASE("第 5 條：查檔不跟隨 junction（核心 checkContentPath）；現在的資料夾本身是 junction "
          "時照常可用") {
    Fixture f;
    const QString real = f.native(QStringLiteral("真的"));
    f.write(QStringLiteral("真的/a.flac"), "x");
    f.write(QStringLiteral("上層/說明.txt"), "x");
    // junction 只建在測試暫存裡（指向同一個暫存裡的資料夾）
    const auto junction = [&](const QString& name, const QString& target) {
        const QString link = f.native(name);
        QProcess mklink;
        mklink.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("mklink"),
                                                 QStringLiteral("/J"), link, target});
        REQUIRE(mklink.waitForFinished(10000));
        REQUIRE(mklink.exitCode() == 0);
        return link;
    };
    const QString start = junction(QStringLiteral("起點"), real);
    const QString inner = junction(QStringLiteral("上層/連結"), real);
    using auralis::mod::pl::contentPathExists;
    CHECK(contentPathExists(real + QStringLiteral("\\a.flac"), QString()));
    // 路徑裡有一層是 junction：算找不到（不查連結另一邊）
    CHECK_FALSE(contentPathExists(start + QStringLiteral("\\a.flac"), QString()));
    CHECK_FALSE(contentPathExists(inner + QStringLiteral("\\a.flac"), QString()));
    // 使用者給的起點本身是 junction：起點不檢查
    CHECK(contentPathExists(start + QStringLiteral("\\a.flac"), start));

    // 重新對應：現在的資料夾是 junction → 找到；現在的資料夾底下有 junction → 維持找不到
    const qint64 id = makeList(f, {{0, {QStringLiteral("G:\\舊\\a.flac"), 0}, true},
                                   {1, {QStringLiteral("G:\\舊2\\連結\\a.flac"), 0}, true}});
    f.importer.setExistsCheck(contentPathExists);
    f.importer.openRemap(id);
    f.importer.applyRemap(QStringLiteral("G:\\舊"), start);
    f.importer.applyRemap(QStringLiteral("G:\\舊2"), f.native(QStringLiteral("上層")));
    CHECK(f.paths(id) ==
          QStringList{start + QStringLiteral("\\a.flac"), QStringLiteral("G:\\舊2\\連結\\a.flac")});
    CHECK(missingFlags(f, id) == QList<bool>{false, true});
    // 之後的重新檢查以存過的規則的現在的資料夾為起點：不會被誤判成找不到
    REQUIRE(auralis::mod::pl::recheckAll(f.store, contentPathExists).hasValue());
    CHECK(missingFlags(f, id) == QList<bool>{false, true});
}

TEST_CASE("情況 2：4 萬首的重新檢查時間（工作執行緒；真的查磁碟）", "[.][perf]") {
    Fixture f;
    const QString exists = f.write(QStringLiteral("在.flac"), "x");
    QList<PlaylistItem> items;
    for (int i = 0; i < 40'000; ++i) {
        items.append({i,
                      {i % 2 == 0 ? exists : f.native(QStringLiteral("不在\\%1.flac").arg(i)), 0},
                      i % 2 == 0});
    }
    const qint64 id = makeList(f, items);
    QElapsedTimer timer;
    timer.start();
    const auto changed = auralis::mod::pl::recheckAll(f.store, auralis::mod::pl::contentPathExists);
    const qint64 ms = timer.elapsed();
    REQUIRE(changed.hasValue());
    CHECK(changed.value() == QList<qint64>{id});
    WARN("4 萬首重新檢查（一半要改 missing）：" << ms << " ms");
}

TEST_CASE("F-1：重新檢查全部、重新檢查一份、重新對應改了項目時，側欄的曲目數跟著更新") {
    Fixture f;
    const qint64 id = makeList(f, {{0, {QStringLiteral("D:\\後來\\a.flac"), 0}, true},
                                   {1, {QStringLiteral("G:\\舊\\b.flac"), 0}, true}});
    f.controller.reload();
    const auto sidebarCount = [&] {
        return f.controller.info(id).value(QStringLiteral("count"));
    };
    REQUIRE(sidebarCount() == 2);
    // 掃描後展開整張 CUE 這類改了項目數的情況：資料庫先多一列，側欄還不知道
    const auto addBehind = [&](const QString& path) {
        REQUIRE(f.store.addTracks(id, {{path, 0}}).hasValue());
    };

    addBehind(QStringLiteral("D:\\c.flac"));
    f.existing = {QStringLiteral("D:\\後來\\a.flac")};
    f.importer.recheckAll();
    CHECK(sidebarCount() == 3);

    addBehind(QStringLiteral("D:\\d.flac"));
    f.existing.clear();
    f.importer.openRemap(id); // a 不見了：重新檢查改了這一份
    CHECK(sidebarCount() == 4);

    addBehind(QStringLiteral("D:\\e.flac"));
    f.existing = {QStringLiteral("D:\\新\\b.flac")};
    f.importer.applyRemap(QStringLiteral("G:\\舊"), QStringLiteral("D:\\新"));
    CHECK(sidebarCount() == 5);
}
