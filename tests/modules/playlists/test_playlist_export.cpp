// 匯出 .m3u8 的內容與寫檔（T-54，US-14 AC-1、2）：格式、相對與絕對路徑、
// 往返（匯出再用 T-51 的匯入讀回，曲目與順序相同；test1.fpl 也一樣）、CUE 曲目、
// 被拒絕的原文（網路路徑只寫文字、不查詢）、先寫暫存檔再改名（失敗時原檔不動、暫存檔不留）。
#include "pl_export_test_helpers.h"
#include "pl_fpl_test_helpers.h"
#include "playlist_export.h"

#include <catch2/generators/catch_generators.hpp>

#include <windows.h>

using auralis::mod::pl::buildM3u8;
using auralis::mod::pl::ExportLine;
using auralis::mod::pl::exportLines;
using auralis::mod::pl::exportPathFor;
using auralis::mod::pl::PlaylistItem;
using auralis::mod::pl::writeExportFile;
using auralis::test::ExportLibrary;
using auralis::test::filesIn;
using auralis::test::fixture;
using auralis::test::readAll;
using auralis::test::Samples;
using Fixture = auralis::test::ImportFixture;

namespace {

/// 照順序的身分「路徑|subsong」（被拒絕的項目存的是原文）。
QStringList refsOf(const QList<PlaylistItem>& items) {
    QStringList refs;
    for (const PlaylistItem& item : items) {
        refs.append(item.track.path + QLatin1Char('|') + QString::number(item.track.subsong));
    }
    return refs;
}

/// 匯出 items 到 target（照模組的流程：讀音樂庫 → 內容 → 寫檔），再用 T-51 的匯入讀回，
/// 回傳讀回來的播放清單的項目。
QList<PlaylistItem> roundTrip(Fixture& f, const QList<PlaylistItem>& items,
                              const ExportLibrary& library, const QString& target, bool relative) {
    const auto content = exportLines(items, &library);
    const QString dir = QFileInfo(target).absolutePath();
    REQUIRE(writeExportFile(target, buildM3u8(content.lines, dir, relative)));
    const qint64 before = f.lastId();
    f.importer.importFiles({target});
    // 有找不到的時匯入會開結果對話框，關掉它（項目已經存好）
    f.importer.dismiss();
    const qint64 id = f.lastId();
    REQUIRE(id != before);
    return f.store.items(id).value();
}

} // namespace

TEST_CASE("AC-14-1：UTF-8 無 BOM、#EXTM3U、#EXTINF:<秒>,<藝人> - <曲名> 接路徑，換行 CRLF") {
    const QList<ExportLine> lines{
        {QStringLiteral("C:\\音樂\\周杰倫\\晴天.flac"), 269, QStringLiteral("周杰倫 - 晴天"),
         false},
        {QStringLiteral("C:\\音樂\\缺\\不見了.flac"), -1, QStringLiteral("不見了.flac"), false}};
    const QByteArray bytes = buildM3u8(lines, QStringLiteral("D:\\清單"), false);
    CHECK(bytes == QStringLiteral("#EXTM3U\r\n"
                                  "#EXTINF:269,周杰倫 - 晴天\r\n"
                                  "C:\\音樂\\周杰倫\\晴天.flac\r\n"
                                  "#EXTINF:-1,不見了.flac\r\n"
                                  "C:\\音樂\\缺\\不見了.flac\r\n")
                       .toUtf8());
    CHECK_FALSE(bytes.startsWith("\xEF\xBB\xBF"));
}

TEST_CASE("AC-14-1：秒數與藝人、曲名取自音樂庫；找不到的照存的路徑、#EXTINF:-1,<檔名>") {
    ExportLibrary library;
    library.add(QStringLiteral("C:\\m\\a.flac"), 0, QStringLiteral("藝人"), QStringLiteral("曲名"),
                183'600);
    // 沒有藝人時只寫曲名；曲名裡的換行換成空白（不讓一首拆成兩行）
    library.add(QStringLiteral("C:\\m\\b.flac"), 0, QString(), QStringLiteral("第一行\r\n第二行"),
                1'000);
    const QList<PlaylistItem> items{{0, {QStringLiteral("C:\\m\\a.flac"), 0}, false},
                                    {1, {QStringLiteral("C:\\m\\b.flac"), 0}, false},
                                    {2, {QStringLiteral("C:\\m\\不在.flac"), 0}, true}};
    const auto content = exportLines(items, &library);
    REQUIRE(content.lines.size() == 3);
    CHECK(content.lines.at(0).seconds == 184);
    CHECK(content.lines.at(0).title == QStringLiteral("藝人 - 曲名"));
    CHECK(content.lines.at(0).path == QStringLiteral("C:\\m\\a.flac"));
    CHECK(content.lines.at(1).title == QStringLiteral("第一行 第二行"));
    CHECK(content.lines.at(2).seconds == -1);
    CHECK(content.lines.at(2).title == QStringLiteral("不在.flac"));
    CHECK(content.lines.at(2).path == QStringLiteral("C:\\m\\不在.flac"));
    CHECK_FALSE(content.hasCue);
}

TEST_CASE("AC-14-1：相對路徑相對於匯出檔所在資料夾；不同磁碟機與讀回會變的寫完整路徑") {
    const QString dir = QStringLiteral("C:\\清單\\匯出");
    CHECK(exportPathFor(QStringLiteral("C:\\清單\\匯出\\a.flac"), dir, true) ==
          QStringLiteral("a.flac"));
    CHECK(exportPathFor(QStringLiteral("C:\\音樂\\專輯\\a.flac"), dir, true) ==
          QStringLiteral("..\\..\\音樂\\專輯\\a.flac"));
    CHECK(exportPathFor(QStringLiteral("c:\\音樂\\a.flac"), dir, true) ==
          QStringLiteral("..\\..\\音樂\\a.flac"));
    CHECK(exportPathFor(QStringLiteral("D:\\音樂\\a.flac"), dir, true) ==
          QStringLiteral("D:\\音樂\\a.flac"));
    // # 開頭會被當成註解、前面的空白讀回時會被去掉：寫完整路徑
    CHECK(exportPathFor(QStringLiteral("C:\\清單\\匯出\\#1.flac"), dir, true) ==
          QStringLiteral("C:\\清單\\匯出\\#1.flac"));
    CHECK(exportPathFor(QStringLiteral("C:\\清單\\匯出\\ 空白\\a.flac"), dir, true) ==
          QStringLiteral("C:\\清單\\匯出\\ 空白\\a.flac"));
    CHECK(exportPathFor(QStringLiteral("C:\\音樂\\a.flac"), dir, false) ==
          QStringLiteral("C:\\音樂\\a.flac"));
}

TEST_CASE("AC-14-2：往返：一般曲目、同一首兩次、找不到、Unicode、被拒絕的原文，相對與絕對都相同") {
    const bool relative = GENERATE(false, true);
    INFO(relative);
    Fixture f;
    ExportLibrary library;
    const QString a = f.native(QStringLiteral("音樂/專輯/01 a.flac"));
    const QString unicode = f.native(QStringLiteral("音樂/ジャズ/曲 ① 𝄞.flac"));
    const QString missing = f.native(QStringLiteral("音樂/不見了.flac"));
    const QString beside = f.native(QStringLiteral("清單/同資料夾.flac"));
    const QString otherDrive = QStringLiteral("Z:\\別的磁碟\\b.flac");
    const QString unc = QStringLiteral("\\\\NAS\\share\\c.flac");
    library.add(a, 0, QStringLiteral("藝人"), QStringLiteral("一"), 60'000);
    library.add(unicode, 0, QStringLiteral("アーティスト"), QStringLiteral("曲"), 61'000);
    const QList<PlaylistItem> items{{0, {a, 0}, false},      {1, {unicode, 0}, false},
                                    {2, {a, 0}, false},      {3, {missing, 0}, true},
                                    {4, {beside, 0}, false}, {5, {otherDrive, 0}, true},
                                    {6, {unc, 0}, true}};
    const QString target = f.native(QStringLiteral("清單/往返.m3u8"));
    QDir().mkpath(QFileInfo(target).absolutePath());
    const auto back = roundTrip(f, items, library, target, relative);
    CHECK(refsOf(back) == refsOf(items));

    const QString text = QString::fromUtf8(readAll(target));
    if (relative) {
        CHECK(text.contains(QStringLiteral("\r\n..\\音樂\\專輯\\01 a.flac\r\n")));
        CHECK(text.contains(QStringLiteral("\r\n同資料夾.flac\r\n")));
    } else {
        CHECK(text.contains(QStringLiteral("\r\n") + a + QStringLiteral("\r\n")));
    }
    CHECK(text.contains(QStringLiteral("\r\nZ:\\別的磁碟\\b.flac\r\n")));
}

TEST_CASE(
    "AC-14-2（S-5）：往返：藝人、曲名標籤含 CR、LF、CRLF 時 #EXTINF 仍是一行，讀回的曲目相同") {
    Fixture f;
    ExportLibrary library;
    const QString a = f.native(QStringLiteral("音樂/a.flac"));
    const QString b = f.native(QStringLiteral("音樂/b.flac"));
    // 換行後面接的文字如果被當成一行，讀回時會變成多出來的「路徑」
    library.add(a, 0, QStringLiteral("藝人\r\nD:\\假的\\x.flac"), QStringLiteral("曲\n名"), 60'000);
    library.add(b, 0, QStringLiteral("藝\r人"), QStringLiteral("曲名\r\n#註解\nC:\\y.flac"),
                61'000);
    const QList<PlaylistItem> items{{0, {a, 0}, false}, {1, {b, 0}, false}};
    const QString target = f.native(QStringLiteral("清單/換行.m3u8"));
    QDir().mkpath(QFileInfo(target).absolutePath());
    const auto back = roundTrip(f, items, library, target, false);
    CHECK(refsOf(back) == refsOf(items));
    const QStringList lines =
        QString::fromUtf8(readAll(target)).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    CHECK(lines.size() == 5);
    CHECK(lines.at(1) == QStringLiteral("#EXTINF:60,藝人 D:\\假的\\x.flac - 曲 名"));
    CHECK(lines.at(3) == QStringLiteral("#EXTINF:61,藝 人 - 曲名 #註解 C:\\y.flac"));
    for (const QString& line : lines) {
        CHECK_FALSE(line.contains(QLatin1Char('\r')));
        CHECK_FALSE(line.contains(QLatin1Char('\n')));
    }
}

namespace {

/// AC-14-2：test1.fpl 匯入的清單匯出再匯入。
void checkTest1RoundTrip(Samples samples, bool relative) {
    INFO(relative);
    Fixture f;
    f.everythingExists = true;
    ExportLibrary library;
    f.importer.importFiles({fixture(samples, QStringLiteral("test1.fpl"))});
    const auto items = f.store.items(f.lastId()).value();
    REQUIRE(items.size() == 40);
    const QString target = f.native(QStringLiteral("test1 匯出.m3u8"));
    const auto back = roundTrip(f, items, library, target, relative);
    CHECK(refsOf(back) == refsOf(items));
}

} // namespace

TEST_CASE("AC-14-2：test1.fpl 匯入的清單匯出再匯入，曲目與順序相同（相對與絕對）") {
    checkTest1RoundTrip(Samples::Public, GENERATE(false, true));
}

TEST_CASE("AC-14-2（私有真樣本）：test1.fpl 匯入的清單匯出再匯入，曲目與順序相同",
          "[private-fixtures]") {
    auralis::test::skipWithoutPrivateSamples();
    checkTest1RoundTrip(Samples::Private, GENERATE(false, true));
}

TEST_CASE("AC-14-1：CUE 曲目匯出成整軌檔的路徑加該軌的 #EXTINF，並標記 hasCue") {
    ExportLibrary library;
    const QString cue = QStringLiteral("C:\\音樂\\專輯\\專輯.cue");
    library.add(cue, 3, QStringLiteral("藝人"), QStringLiteral("第三軌"), 245'000);
    library.audioOf.insert(cue, QStringLiteral("C:\\音樂\\專輯\\專輯.flac"));
    const QList<PlaylistItem> items{{0, {cue, 3}, false}};
    const auto content = exportLines(items, &library);
    REQUIRE(content.lines.size() == 1);
    CHECK(content.hasCue);
    CHECK(content.lines.first().path == QStringLiteral("C:\\音樂\\專輯\\專輯.flac"));
    CHECK(content.lines.first().seconds == 245);
    CHECK(content.lines.first().title == QStringLiteral("藝人 - 第三軌"));
    CHECK(buildM3u8(content.lines, QStringLiteral("C:\\音樂"), true) ==
          QStringLiteral("#EXTM3U\r\n#EXTINF:245,藝人 - 第三軌\r\n專輯\\專輯.flac\r\n").toUtf8());
}

TEST_CASE("AC-14-1：被路徑規則拒絕的原文照原文寫、不查音樂庫，相對模式也不改") {
    ExportLibrary library;
    const QString unc = QStringLiteral("\\\\NAS\\音樂\\a.flac");
    const QList<PlaylistItem> items{{0, {unc, 0}, true}};
    const auto content = exportLines(items, &library);
    REQUIRE(content.lines.size() == 1);
    CHECK(content.lines.first().verbatim);
    CHECK(content.lines.first().title == QStringLiteral("a.flac"));
    CHECK(library.queried.isEmpty());
    CHECK(library.audioCalls.isEmpty());
    CHECK(buildM3u8(content.lines, QStringLiteral("C:\\清單"), true) ==
          QStringLiteral("#EXTM3U\r\n#EXTINF:-1,a.flac\r\n\\\\NAS\\音樂\\a.flac\r\n").toUtf8());
}

TEST_CASE("寫使用者檔案：取代已存在的檔，只留下那一個檔、不留暫存檔") {
    Fixture f;
    const QString folder = f.native(QStringLiteral("匯出"));
    QDir().mkpath(folder);
    const QString target = f.write(QStringLiteral("匯出/清單.m3u8"), "old");
    REQUIRE(writeExportFile(target, "#EXTM3U\r\n"));
    CHECK(readAll(target) == QByteArray("#EXTM3U\r\n"));
    CHECK(filesIn(folder) == QStringList{QStringLiteral("清單.m3u8")});
}

TEST_CASE("寫使用者檔案：目標被鎖住無法取代時回傳錯誤，原檔不動、暫存檔不留") {
    Fixture f;
    const QString folder = f.native(QStringLiteral("鎖住"));
    QDir().mkpath(folder);
    const QString target = f.write(QStringLiteral("鎖住/清單.m3u8"), "原本的內容");
    // 只允許共用讀取（不允許刪除、改名）：改名取代一定失敗
    const HANDLE lock =
        CreateFileW(reinterpret_cast<const wchar_t*>(target.utf16()), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(lock != INVALID_HANDLE_VALUE);
    const auto written = writeExportFile(target, "#EXTM3U\r\n新的\r\n");
    CloseHandle(lock);
    CHECK_FALSE(written);
    CHECK(readAll(target) == QByteArray("原本的內容"));
    CHECK(filesIn(folder) == QStringList{QStringLiteral("清單.m3u8")});
}

TEST_CASE("寫使用者檔案：資料夾不存在時回傳錯誤，什麼都不建立") {
    Fixture f;
    const QString target = f.native(QStringLiteral("沒有這個資料夾/清單.m3u8"));
    CHECK_FALSE(writeExportFile(target, "#EXTM3U\r\n"));
    CHECK_FALSE(QFileInfo::exists(QFileInfo(target).absolutePath()));
}

TEST_CASE("M-04 審查（疑點 2）：存著的路徑含控制字元時不寫路徑行，改寫一行 # 註解並計數") {
    const QString inject = QStringLiteral("http://x/a.flac\r\n\\\\evil-host\\share\\x.flac");
    const QString nul = QStringLiteral("C:\\m\\a") + QChar(u'\0') + QStringLiteral("b.flac");
    const QList<PlaylistItem> items{{0, {inject, 0}, true},
                                    {1, {QStringLiteral("C:\\m\\ok.flac"), 0}, true},
                                    {2, {nul, 0}, true},
                                    {3, {QStringLiteral("C:\\m\\t\tab.flac"), 0}, false}};
    const auto content = exportLines(items, nullptr);
    CHECK(content.invalidPaths == 3);
    const QString fffd(QChar(0xFFFD));
    CHECK(buildM3u8(content.lines, QStringLiteral("C:\\清單"), false) ==
          (QStringLiteral("#EXTM3U\r\n"
                          "#路徑含無效字元，沒有匯出：http://x/a.flac") +
           fffd + fffd +
           QStringLiteral("\\\\evil-host\\share\\x.flac\r\n"
                          "#EXTINF:-1,ok.flac\r\n"
                          "C:\\m\\ok.flac\r\n"
                          "#路徑含無效字元，沒有匯出：C:\\m\\a") +
           fffd +
           QStringLiteral("b.flac\r\n"
                          "#路徑含無效字元，沒有匯出：C:\\m\\t") +
           fffd + QStringLiteral("ab.flac\r\n"))
              .toUtf8());
}

TEST_CASE(
    "M-04 審查（疑點 2）：.fpl 的路徑夾 CR LF：匯入 → 匯出不會多出 \\\\ 開頭的行，再匯入照舊") {
    const bool relative = GENERATE(false, true);
    INFO(relative);
    Fixture f;
    const QString file =
        f.write(QStringLiteral("注入.fpl"),
                auralis::test::buildFpl({{"http://x/a.flac\r\n\\\\evil-host\\share\\x.flac"},
                                         {"C:\\a\r\nC:\\b.flac"},
                                         {"file://C:\\音樂\\本機.flac"}}));
    f.existing = {QStringLiteral("C:\\音樂\\本機.flac")};
    f.importer.importFiles({file});
    const QVariantMap result = f.importer.result();
    const QVariantList missing = result.value(QStringLiteral("missingItems")).toList();
    REQUIRE(missing.size() == 2);
    for (const QVariant& item : missing) {
        CHECK(item.toMap().value(QStringLiteral("reason")) == QStringLiteral("路徑含無效字元"));
    }
    f.importer.dismiss();
    const auto items = f.store.items(result.value(QStringLiteral("id")).toLongLong()).value();
    REQUIRE(items.size() == 3);
    // 資料庫裡不再有含換行的路徑：控制字元換成 U+FFFD
    CHECK(items.at(0).track.path == QStringLiteral("http://x/a.flac") + QChar(0xFFFD) +
                                        QChar(0xFFFD) +
                                        QStringLiteral("\\\\evil-host\\share\\x.flac"));
    for (const PlaylistItem& item : items) {
        CHECK_FALSE(item.track.path.contains(QLatin1Char('\r')));
        CHECK_FALSE(item.track.path.contains(QLatin1Char('\n')));
    }

    ExportLibrary library;
    const QString target = f.native(QStringLiteral("清單/注入.m3u8"));
    QDir().mkpath(QFileInfo(target).absolutePath());
    const auto back = roundTrip(f, items, library, target, relative);
    CHECK(refsOf(back) == refsOf(items));
    const QStringList lines =
        QString::fromUtf8(readAll(target)).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    CHECK(lines.size() == 7); // #EXTM3U＋每首 #EXTINF 與路徑
    for (const QString& line : lines) {
        CHECK_FALSE(line.startsWith(QStringLiteral("\\\\")));
        CHECK(line != QStringLiteral("C:\\b.flac"));
    }
}
