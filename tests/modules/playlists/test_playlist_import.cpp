// 匯入 m3u／m3u8／pls（T-51，US-12 AC-1、4；S-09）：
// 回歸樣本 test2.m3u8；清單內容的網路路徑與裝置名稱拒絕、不查詢、不交給播放；
// 同名加「 (2)」；同一首兩次；讀不到不建立；不是 .fpl 結構的 .fpl 回不支援；只讀不改。
// 樣本在測試暫存裡用程式產生（tests/fixtures 唯讀）；
// test2.m3u8 的路徑用假的檔案查詢（不碰真的磁碟機）；公開的合成樣本一定跑，
// 私有真樣本不在時 SKIP（T-100）。
#include "pl_import_test_helpers.h"
#include "playlist_tracks.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QProcess>

using auralis::mod::pl::PlaylistTracks;
using auralis::test::Samples;
using auralis::test::sha256;
using auralis::test::utf8;
using Fixture = auralis::test::ImportFixture;

namespace {

/// AC-12-1：test2.m3u8（BOM、CRLF、沒有 #EXTM3U、開頭一行 #）。
void checkTest2(Samples samples) {
    const auto fixture = [samples](const QString& name) {
        return auralis::test::fixture(samples, name);
    };
    Fixture f;
    f.everythingExists = true;
    // 對照：直接從位元組讀（去 BOM、依 CRLF 切行、略過 # 行），不經解析器
    QFile raw(fixture(QStringLiteral("test2.m3u8")));
    REQUIRE(raw.open(QIODevice::ReadOnly));
    const QByteArray bytes = raw.readAll();
    REQUIRE(bytes.startsWith("\xEF\xBB\xBF"));
    QStringList expected;
    for (const QByteArray& line : bytes.mid(3).split('\n')) {
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty() && !text.startsWith(QLatin1Char('#'))) {
            expected.append(text);
        }
    }
    REQUIRE(expected.size() == 40);

    const QByteArray before = sha256(fixture(QStringLiteral("test2.m3u8")));
    f.importer.importFiles({fixture(QStringLiteral("test2.m3u8"))});
    const qint64 id = f.lastId();
    REQUIRE(id > 0);
    CHECK(f.paths(id) == expected);
    CHECK(f.store.playlist(id).value()->name == QStringLiteral("test2"));
    // 全部找到：不開對話框，開新播放清單頁並通知
    CHECK(f.importer.result().isEmpty());
    CHECK(f.navigator.opened == QStringList{QStringLiteral("playlist %1").arg(id)});
    CHECK(f.notifier.texts.last() == QStringLiteral("已匯入「test2」，共 40 首"));
    // AC-12-4：只讀不改
    CHECK(sha256(fixture(QStringLiteral("test2.m3u8"))) == before);
}

} // namespace

TEST_CASE("AC-12-1：test2.m3u8（BOM、CRLF、沒有 #EXTM3U）匯入成 40 首、順序與路徑相同") {
    checkTest2(Samples::Public);
}

TEST_CASE("AC-12-1（私有真樣本）：test2.m3u8 匯入成 40 首、順序與路徑相同", "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest2(Samples::Private);
}

TEST_CASE("安全底線：UNC 樣本匯入時被拒絕的路徑記成找不到並說明，而且沒有被查詢") {
    Fixture f;
    const QString local = f.write(QStringLiteral("音樂/本機.flac"), "x");
    f.existing = {local};
    const QString list =
        f.write(QStringLiteral("unc.m3u8"),
                utf8("#EXTM3U\n\\\\evil-host\\share\\a.flac\n//evil-host/share/b.flac\n"
                     "file://evil-host/share/c.flac\nfile:////evil-host/share/d.flac\n"
                     "\\\\?\\UNC\\evil-host\\share\\e.flac\nC:\\x\\NUL\n"
                     "音樂\\本機.flac\n"));
    f.importer.importFiles({list});
    for (const QString& path : f.queried) {
        INFO(path.toStdString());
        CHECK_FALSE(path.contains(QStringLiteral("evil-host")));
        CHECK_FALSE(path.startsWith(QStringLiteral("\\\\")));
        CHECK_FALSE(path.contains(QStringLiteral("NUL")));
    }
    // 只查了本機那一首（情況 3：音樂資料夾以外的本機檔案可以）
    CHECK(f.queried == QStringList{local});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("total")).toInt() == 7);
    CHECK(result.value(QStringLiteral("found")).toInt() == 1);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 6);
    const QVariantList missing = result.value(QStringLiteral("missingItems")).toList();
    CHECK(missing.first().toMap().value(QStringLiteral("reason")).toString() ==
          QStringLiteral("網路上的路徑，不會開啟"));
    // 被拒絕的照原文保留在清單裡（標 missing），但不交給播放
    const qint64 id = result.value(QStringLiteral("id")).toLongLong();
    const auto items = f.store.items(id).value();
    REQUIRE(items.size() == 7);
    CHECK(items.first().missing);
    CHECK_FALSE(items.last().missing);
    REQUIRE(f.controller.play(id));
    CHECK(f.playback.paths() == QStringList{local});
}

TEST_CASE("安全底線：播放清單頁的播放、右鍵與拖出去都不交出被拒絕的路徑") {
    Fixture f;
    const QString local = f.write(QStringLiteral("本機.flac"), "x");
    f.existing = {local};
    f.importer.importFiles({f.write(QStringLiteral("混.m3u8"),
                                    utf8("本機.flac\n\\\\evil-host\\share\\a.flac\n本機.flac\n"))});
    const qint64 id = f.importer.result().value(QStringLiteral("id")).toLongLong();
    REQUIRE(id > 0);
    PlaylistTracks tracks(f.store, f.controller, nullptr, &f.playback);
    tracks.setPlaylistId(id);
    QElapsedTimer timer;
    timer.start();
    while (tracks.status() != QStringLiteral("ready") && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    REQUIRE(tracks.count() == 3);
    // 被拒絕的那一列不能播；從第三列播時起點跟著往前算
    CHECK_FALSE(tracks.playFrom(1));
    REQUIRE(tracks.playFrom(2));
    CHECK(f.playback.paths() == QStringList{local, local});
    CHECK(f.playback.currentIndex() == 1);
    tracks.selectAll();
    CHECK(tracks.selectedTracks().size() == 2);
    CHECK_FALSE(tracks.payloadJson().contains(QStringLiteral("evil-host")));
}

TEST_CASE("M-04 審查（附帶 A）：標題列「播放」在第一首找不到時從第一首找得到的開始；全部找不到時"
          "不播並通知") {
    Fixture f;
    const QString local = f.write(QStringLiteral("本機.flac"), "x");
    f.existing = {local};
    const auto load = [&f](const QString& name, const QByteArray& text) {
        f.importer.importFiles({f.write(name, text)});
        const qint64 id = f.importer.result().value(QStringLiteral("id")).toLongLong();
        f.importer.dismiss();
        auto tracks = std::make_unique<PlaylistTracks>(f.store, f.controller, nullptr, &f.playback);
        tracks->setPlaylistId(id);
        QElapsedTimer timer;
        timer.start();
        while (tracks->status() != QStringLiteral("ready") && timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
        }
        return tracks;
    };
    const auto first =
        load(QStringLiteral("第一首不見.m3u8"), utf8("\\\\evil-host\\share\\a.flac\n本機.flac\n"));
    REQUIRE(first->count() == 2);
    REQUIRE(first->playAll());
    CHECK(f.playback.paths() == QStringList{local});
    CHECK(f.playback.currentIndex() == 0);

    const auto none = load(QStringLiteral("全部不見.m3u8"), utf8("\\\\evil-host\\share\\a.flac\n"));
    REQUIRE(none->count() == 1);
    const qsizetype calls = f.playback.calls().size();
    CHECK_FALSE(none->playAll());
    CHECK(f.playback.calls().size() == calls);
    CHECK(f.notifier.texts.last() == QStringLiteral("這個播放清單的曲目都找不到"));
}

TEST_CASE("安全底線（S-1）：路徑經過 junction 的項目記成找不到並說明原因，四個出口（播放、右鍵、"
          "拖出、側欄播放）都不交出") {
    Fixture f;
    f.importer.setExistsCheck(auralis::mod::pl::contentPathExists, auralis::mod::pl::linkReason);
    const QString real = f.write(QStringLiteral("真的/a.flac"), "x");
    // junction 只建在測試暫存裡（指向同一個暫存裡的資料夾）；核心開檔會跟隨它，連結可以指到網路
    const QString link = f.native(QStringLiteral("連結"));
    QProcess mklink;
    mklink.start(QStringLiteral("cmd.exe"),
                 {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"), link,
                  f.native(QStringLiteral("真的"))});
    REQUIRE(mklink.waitForFinished(10000));
    REQUIRE(mklink.exitCode() == 0);
    f.importer.importFiles(
        {f.write(QStringLiteral("連結.m3u8"), utf8("真的\\a.flac\n連結\\a.flac\n"))});
    const QVariantMap result = f.importer.result();
    REQUIRE(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    const QString reason = QStringLiteral("路徑經過符號連結或 junction，不會開啟");
    const QVariantList missing = result.value(QStringLiteral("missingItems")).toList();
    REQUIRE(missing.size() == 1);
    CHECK(missing.first().toMap().value(QStringLiteral("reason")).toString() == reason);
    const qint64 id = result.value(QStringLiteral("id")).toLongLong();
    REQUIRE(id > 0);

    // 側欄播放
    REQUIRE(f.controller.play(id));
    CHECK(f.playback.paths() == QStringList{real});
    // 播放清單頁的播放、右鍵、拖出
    PlaylistTracks tracks(f.store, f.controller, nullptr, &f.playback);
    tracks.setPlaylistId(id);
    QElapsedTimer timer;
    timer.start();
    while (tracks.status() != QStringLiteral("ready") && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    REQUIRE(tracks.count() == 2);
    CHECK_FALSE(tracks.playFrom(1));
    REQUIRE(tracks.playFrom(0));
    CHECK(f.playback.paths() == QStringList{real});
    tracks.selectAll();
    CHECK(tracks.selectedTracks().size() == 1);
    CHECK_FALSE(tracks.payloadJson().contains(QStringLiteral("連結")));

    // 重新檢查後的 S-09 也寫同一個原因
    f.importer.dismiss();
    f.importer.openRemap(id);
    const QVariantList rechecked =
        f.importer.result().value(QStringLiteral("missingItems")).toList();
    REQUIRE(rechecked.size() == 1);
    CHECK(rechecked.first().toMap().value(QStringLiteral("reason")).toString() == reason);
}

TEST_CASE("情況 1、4：同一首兩次都保留；同名播放清單加「 (2)」「 (3)」") {
    Fixture f;
    f.everythingExists = true;
    const QString list =
        f.write(QStringLiteral("通勤.m3u8"), utf8("E:\\a.flac\nE:\\b.flac\nE:\\a.flac\n"));
    f.importer.importFiles({list, list, list});
    const auto all = f.store.list().value();
    REQUIRE(all.size() == 3);
    CHECK(all.at(0).name == QStringLiteral("通勤"));
    CHECK(all.at(1).name == QStringLiteral("通勤 (2)"));
    CHECK(all.at(2).name == QStringLiteral("通勤 (3)"));
    CHECK(f.paths(all.at(2).id) == QStringList{QStringLiteral("E:\\a.flac"),
                                               QStringLiteral("E:\\b.flac"),
                                               QStringLiteral("E:\\a.flac")});
    CHECK(f.notifier.texts.last() == QStringLiteral("已匯入「通勤 (3)」，共 3 首"));
}

TEST_CASE("S-09：有找不到的開結果、略過後才做下一個檔；讀不到不建立；壞的 .fpl 回不支援") {
    Fixture f;
    const QString found = f.write(QStringLiteral("有.flac"), "x");
    f.existing = {found};
    const QString partly = f.write(QStringLiteral("部分.m3u8"), utf8("有.flac\n沒有.flac\n"));
    const QString fpl = f.write(QStringLiteral("舊.fpl"), "fpl");
    const QString absent = f.native(QStringLiteral("不存在.m3u"));
    f.importer.importFiles({partly, absent, fpl});

    QVariantMap result = f.importer.result();
    CHECK(result.value(QStringLiteral("kind")) == QStringLiteral("missing"));
    CHECK(result.value(QStringLiteral("fileName")) == QStringLiteral("部分.m3u8"));
    CHECK(result.value(QStringLiteral("found")).toInt() == 1);
    CHECK(result.value(QStringLiteral("missing")).toInt() == 1);
    CHECK(result.value(QStringLiteral("missingItems"))
              .toList()
              .first()
              .toMap()
              .value(QStringLiteral("path")) == f.native(QStringLiteral("沒有.flac")));
    CHECK(f.navigator.opened.isEmpty());
    CHECK(f.store.list().value().size() == 1);
    // 略過：找不到的那一筆保留在清單裡
    const qint64 id = result.value(QStringLiteral("id")).toLongLong();
    CHECK(f.store.items(id).value().last().missing);

    f.importer.dismiss();
    result = f.importer.result();
    CHECK(result.value(QStringLiteral("kind")) == QStringLiteral("unreadable"));
    CHECK(result.value(QStringLiteral("message")) ==
          QStringLiteral("無法讀取 不存在.m3u，請確認檔案存在而且沒有被其他程式鎖住。"));
    CHECK(f.store.list().value().size() == 1);

    f.importer.dismiss();
    result = f.importer.result();
    CHECK(result.value(QStringLiteral("kind")) == QStringLiteral("unsupported"));
    CHECK(f.store.list().value().size() == 1);
    f.importer.dismiss();
    CHECK(f.importer.result().isEmpty());
    CHECK_FALSE(f.importer.busy());
}

TEST_CASE("S-09：側欄「匯入播放清單」開檔對話框（篩選四種副檔名），選了就匯入") {
    Fixture f;
    f.everythingExists = true;
    f.importer.chooseFiles();
    CHECK(f.dialogs.lastTitle == QStringLiteral("匯入播放清單"));
    CHECK(f.dialogs.lastFilters ==
          QStringList{QStringLiteral("播放清單 (*.m3u *.m3u8 *.pls *.fpl)")});
    REQUIRE(f.dialogs.pending);
    f.dialogs.pending({f.write(QStringLiteral("選的.m3u"), utf8("E:\\a.flac\n"))});
    CHECK(f.store.list().value().size() == 1);
}

TEST_CASE("AC-12-4：匯入前後播放清單檔的雜湊不變（產生的樣本）") {
    Fixture f;
    f.everythingExists = true;
    const QString m3u = f.write(QStringLiteral("a.m3u"), utf8("#\r\nE:\\a.flac\r\n"));
    const QString pls = f.write(QStringLiteral("b.pls"), utf8("[playlist]\nFile1=E:\\a.flac\n"));
    const QByteArray m3uBefore = sha256(m3u);
    const QByteArray plsBefore = sha256(pls);
    const QDateTime m3uTime = QFileInfo(m3u).lastModified();
    f.importer.importFiles({m3u, pls});
    CHECK(f.store.list().value().size() == 2);
    CHECK(sha256(m3u) == m3uBefore);
    CHECK(sha256(pls) == plsBefore);
    CHECK(QFileInfo(m3u).lastModified() == m3uTime);
}

TEST_CASE(
    "AC-12-3（M-04 審查）：匯入、開重新對應、套用的背景工作被取消後忙碌狀態清掉，之後照常能做") {
    Fixture f;
    auralis::test::HeldJobs held;
    auralis::mod::pl::PlaylistImporter importer{f.store,    f.controller, f.dialogs,
                                                f.notifier, &held.jobs,   nullptr};
    importer.setExistsCheck(
        [](const QString& path, const QString&) { return !path.contains(QStringLiteral("不在")); });
    QList<qint64> imported;
    QObject::connect(&importer, &auralis::mod::pl::PlaylistImporter::imported,
                     [&imported](const QString&, qint64 id) { imported.append(id); });

    // 匯入兩個檔，第一個被取消：不建立清單、忙碌清掉，佇列的下一個照常匯入
    const QString list = auralis::test::fixture(Samples::Public, QStringLiteral("test2.m3u8"));
    importer.importFiles({list, list});
    CHECK(importer.busy());
    held.cancelAndDrain();
    REQUIRE(imported.size() == 2);
    CHECK(imported.first() == 0);
    CHECK(imported.last() > 0);
    CHECK(f.store.list().value().size() == 1);
    CHECK_FALSE(importer.busy());

    // 開重新對應被取消：忙碌清掉，再開一次照常出結果
    const auto created = f.store.createImported(
        QStringLiteral("清單"), {{0, {QStringLiteral("D:\\不在\\b.flac"), 0}, true}});
    REQUIRE(created.hasValue());
    const qint64 id = created.value().id;
    held.hold();
    importer.openRemap(id);
    CHECK(importer.busy());
    held.cancelAndDrain();
    CHECK_FALSE(importer.busy());
    held.hold();
    importer.openRemap(id);
    held.drain();
    REQUIRE(importer.result().value(QStringLiteral("kind")) == QStringLiteral("missing"));

    // 套用並重新檢查被取消：remapping 清掉，再套用一次照常更新結果
    held.hold();
    importer.applyRemap(QStringLiteral("D:\\不在"), QStringLiteral("D:\\在"));
    CHECK(importer.remapping());
    held.cancelAndDrain();
    CHECK_FALSE(importer.remapping());
    CHECK(importer.result().value(QStringLiteral("remapped")).toInt() == -1);
    held.hold();
    importer.applyRemap(QStringLiteral("D:\\不在"), QStringLiteral("D:\\在"));
    held.drain();
    CHECK_FALSE(importer.remapping());
    // 被取消的那次工作已經做完寫入（只是不套用到畫面），所以這次沒有要改的、數字換成全部找到
    CHECK(importer.result().value(QStringLiteral("remapped")).toInt() == 0);
    CHECK(importer.result().value(QStringLiteral("missing")).toInt() == 0);
    CHECK(f.paths(id) == QStringList{QStringLiteral("D:\\在\\b.flac")});
}
