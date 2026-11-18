// 播放清單檔的解析（T-51，US-12 AC-2）：路徑寫法（絕對、相對、file:/// URI）、清單內容的路徑規則
// （網路路徑、裝置名稱、非本機 file:// 拒絕）、編碼（UTF-16 BOM、Big5、Shift-JIS）、m3u 的 # 行與
// #EXTINF、pls 的 File<n> 亂序。樣本在測試暫存裡用程式產生（tests/fixtures 唯讀）。
#include "pl_import_test_helpers.h"
#include "playlist_parse.h"

using auralis::mod::pl::resolveEntryPath;
using auralis::test::utf8;
using Fixture = auralis::test::ImportFixture;

TEST_CASE("M-04 審查（疑點 2）：路徑中間含控制字元時拒絕（路徑含無效字元），協定與網路路徑也一樣") {
    const QString base = QStringLiteral("D:\\清單");
    for (const QString& raw : {QStringLiteral("C:\\a\r\nC:\\b.flac"),
                               QStringLiteral("http://x/a.flac\r\n\\\\evil-host\\share\\x.flac"),
                               QStringLiteral("C:\\a\tb.flac"), QStringLiteral("相對\x7f.flac"),
                               QStringLiteral("C:\\a") + QChar(u'\0') + QStringLiteral("b.flac")}) {
        INFO(raw.toStdString());
        const auto result = resolveEntryPath(raw, base);
        REQUIRE_FALSE(result);
        CHECK(result.error().message == QStringLiteral("路徑含無效字元"));
    }
}

TEST_CASE("AC-12-2：路徑寫法：絕對、相對（清單檔所在資料夾）、\\ 開頭、/ 分隔、file:/// URI") {
    const QString base = QStringLiteral("D:\\清單\\通勤");
    CHECK(resolveEntryPath(QStringLiteral("E:\\音樂\\a.flac"), base).value() ==
          QStringLiteral("E:\\音樂\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("E:/音樂/./b/../a.flac"), base).value() ==
          QStringLiteral("E:\\音樂\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("子資料夾\\a.flac"), base).value() ==
          QStringLiteral("D:\\清單\\通勤\\子資料夾\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("..\\..\\音樂\\a.flac"), base).value() ==
          QStringLiteral("D:\\音樂\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("\\音樂\\a.flac"), base).value() ==
          QStringLiteral("D:\\音樂\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("file:///E:/%E9%9F%B3%E6%A8%82/a%20b.flac"), base)
              .value() == QStringLiteral("E:\\音樂\\a b.flac"));
    CHECK(resolveEntryPath(QStringLiteral("file://localhost/E:/a.flac"), base).value() ==
          QStringLiteral("E:\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("FILE:///e:/音樂/a.flac"), base).value() ==
          QStringLiteral("e:\\音樂\\a.flac"));
}

TEST_CASE("AC-12-2：清單檔本身在網路上（使用者選的）時，相對路徑只到同一個共用資料夾") {
    const QString base = QStringLiteral("\\\\nas\\share\\lists");
    CHECK(resolveEntryPath(QStringLiteral("a.flac"), base).value() ==
          QStringLiteral("\\\\nas\\share\\lists\\a.flac"));
    CHECK(resolveEntryPath(QStringLiteral("..\\music\\a.flac"), base).value() ==
          QStringLiteral("\\\\nas\\share\\music\\a.flac"));
    CHECK_FALSE(resolveEntryPath(QStringLiteral("..\\..\\other\\a.flac"), base));
    CHECK_FALSE(resolveEntryPath(QStringLiteral("..\\..\\..\\evil\\share\\a.flac"), base));
}

TEST_CASE("安全底線：清單內容的網路路徑、裝置名稱、非本機 file:// 一律拒絕") {
    const QString base = QStringLiteral("D:\\清單");
    const QStringList rejected{
        QStringLiteral("\\\\server\\share\\a.flac"),         // UNC
        QStringLiteral("//server/share/a.flac"),             // / 寫的 UNC
        QStringLiteral("\\\\?\\C:\\a.flac"),                 // 長路徑前綴
        QStringLiteral("\\\\.\\C:\\a.flac"),                 // 裝置命名空間
        QStringLiteral("\\\\?\\UNC\\server\\share\\a.flac"), // 長路徑前綴的 UNC
        QStringLiteral("file://server/share/a.flac"),        // 非本機 file://
        QStringLiteral("file:////server/share/a.flac"),      // file:// 包著 UNC
        QStringLiteral("file:///%5C%5Cserver/share/a.flac"), // 解碼後是 UNC
        QStringLiteral("file:///a.flac"),                    // 不是完整路徑
        QStringLiteral("http://example.com/a.mp3"),          // 其他協定
        QStringLiteral("smb://server/share/a.flac"),         // 其他協定
        QStringLiteral("C:\\音樂\\CON"),                     // 裝置名稱
        QStringLiteral("C:\\音樂\\nul.flac"),                // 裝置名稱加副檔名
        QStringLiteral("COM1"),                              // 相對路徑的裝置名稱
        QStringLiteral("C:\\音樂\\COM¹"),                    // S-3：上標數字的 COM
        QStringLiteral("C:\\音樂\\lpt³.flac"),               // S-3：上標數字的 LPT
        QStringLiteral("CONIN$"),                            // S-3：主控台輸入
        QStringLiteral("C:\\音樂\\conout$.flac"),            // S-3：主控台輸出
        QStringLiteral("C:\\音樂\\a.flac:stream"),           // 替代資料流
        QStringLiteral("C:a.flac"),                          // 只有磁碟機代號的相對路徑
        QStringLiteral("..\\..\\..\\a.flac"),                // 跳出磁碟機根目錄
    };
    for (const QString& raw : rejected) {
        INFO(raw.toStdString());
        CHECK_FALSE(resolveEntryPath(raw, base));
    }
}

TEST_CASE("AC-12-2：編碼：UTF-16 BOM、Big5、Shift-JIS（.m3u 無 BOM）、只有 # 的第一行") {
    Fixture f;
    f.everythingExists = true;
    SECTION("UTF-16LE 與 UTF-16BE BOM") {
        const QString text =
            QStringLiteral("#\r\n#EXTINF:215,周杰倫 - 七里香\r\n音樂\\七里香.flac\r\n");
        QByteArray le("\xFF\xFE", 2);
        QByteArray be("\xFE\xFF", 2);
        for (const QChar c : text) {
            le.append(char(c.unicode() & 0xFF)).append(char(c.unicode() >> 8));
            be.append(char(c.unicode() >> 8)).append(char(c.unicode() & 0xFF));
        }
        f.importer.importFiles({f.write(QStringLiteral("le.m3u"), le)});
        CHECK(f.paths(f.lastId()) == QStringList{f.native(QStringLiteral("音樂/七里香.flac"))});
        f.importer.importFiles({f.write(QStringLiteral("be.m3u8"), be)});
        CHECK(f.paths(f.lastId()) == QStringList{f.native(QStringLiteral("音樂/七里香.flac"))});
    }
    SECTION("Big5（.m3u 無 BOM，TextDecoder）") {
        // 「#」與四行「音樂\<藝人>\<曲名>.flac」（CRLF）的 Big5
        const unsigned char big5[] = {
            0x23, 0x0d, 0x0a, 0xad, 0xb5, 0xbc, 0xd6, 0x5c, 0xa9, 0x50, 0xaa, 0x4e, 0xad,
            0xdb, 0x5c, 0xa4, 0x43, 0xa8, 0xbd, 0xad, 0xbb, 0x2e, 0x66, 0x6c, 0x61, 0x63,
            0x0d, 0x0a, 0xad, 0xb5, 0xbc, 0xd6, 0x5c, 0xb3, 0xaf, 0xab, 0xb3, 0xa8, 0xb3,
            0x5c, 0xa4, 0x51, 0xa6, 0x7e, 0x2e, 0x66, 0x6c, 0x61, 0x63, 0x0d, 0x0a, 0xad,
            0xb5, 0xbc, 0xd6, 0x5c, 0xa4, 0xad, 0xa4, 0xeb, 0xa4, 0xd1, 0x5c, 0xb7, 0xc5,
            0xac, 0x58, 0x2e, 0x66, 0x6c, 0x61, 0x63, 0x0d, 0x0a, 0xad, 0xb5, 0xbc, 0xd6,
            0x5c, 0xbd, 0xb2, 0xa8, 0xcc, 0xaa, 0x4c, 0x5c, 0xa4, 0xe9, 0xa4, 0xa3, 0xb8,
            0xa8, 0x2e, 0x66, 0x6c, 0x61, 0x63, 0x0d, 0x0a};
        f.importer.importFiles(
            {f.write(QStringLiteral("big5.m3u"),
                     QByteArray(reinterpret_cast<const char*>(big5), sizeof(big5)))});
        CHECK(f.paths(f.lastId()) ==
              QStringList{f.native(QStringLiteral("音樂/周杰倫/七里香.flac")),
                          f.native(QStringLiteral("音樂/陳奕迅/十年.flac")),
                          f.native(QStringLiteral("音樂/五月天/溫柔.flac")),
                          f.native(QStringLiteral("音樂/蔡依林/日不落.flac"))});
    }
    SECTION("Shift-JIS（.m3u 無 BOM，TextDecoder）") {
        // 「#」與三行「音楽\<藝人>\<曲名>.flac」（CRLF）的 Shift-JIS
        const unsigned char sjis[] = {
            0x23, 0x0d, 0x0a, 0x89, 0xb9, 0x8a, 0x79, 0x5c, 0x89, 0x46, 0x91, 0xbd, 0x93, 0x63,
            0x83, 0x71, 0x83, 0x4a, 0x83, 0x8b, 0x5c, 0x83, 0x74, 0x83, 0x40, 0x81, 0x5b, 0x83,
            0x58, 0x83, 0x67, 0x81, 0x45, 0x83, 0x89, 0x83, 0x75, 0x2e, 0x66, 0x6c, 0x61, 0x63,
            0x0d, 0x0a, 0x89, 0xb9, 0x8a, 0x79, 0x5c, 0x92, 0xc5, 0x96, 0xbc, 0x97, 0xd1, 0x8c,
            0xe7, 0x5c, 0x8a, 0xdb, 0x82, 0xcc, 0x93, 0xe0, 0x83, 0x54, 0x83, 0x66, 0x83, 0x42,
            0x83, 0x58, 0x83, 0x65, 0x83, 0x42, 0x83, 0x62, 0x83, 0x4e, 0x2e, 0x66, 0x6c, 0x61,
            0x63, 0x0d, 0x0a, 0x89, 0xb9, 0x8a, 0x79, 0x5c, 0x95, 0xc4, 0x92, 0xc3, 0x8c, 0xba,
            0x8e, 0x74, 0x5c, 0x83, 0x41, 0x83, 0x43, 0x83, 0x6c, 0x83, 0x4e, 0x83, 0x89, 0x83,
            0x43, 0x83, 0x6c, 0x2e, 0x66, 0x6c, 0x61, 0x63, 0x0d, 0x0a};
        f.importer.importFiles(
            {f.write(QStringLiteral("sjis.m3u"),
                     QByteArray(reinterpret_cast<const char*>(sjis), sizeof(sjis)))});
        CHECK(f.paths(f.lastId()) ==
              QStringList{f.native(QStringLiteral("音楽/宇多田ヒカル/ファースト・ラブ.flac")),
                          f.native(QStringLiteral("音楽/椎名林檎/丸の内サディスティック.flac")),
                          f.native(QStringLiteral("音楽/米津玄師/アイネクライネ.flac"))});
    }
}

TEST_CASE("AC-12-2：m3u 的 #EXTINF、LF、file:/// URI 與相對路徑混在一起") {
    Fixture f;
    f.everythingExists = true;
    const QString list =
        f.write(QStringLiteral("清單/混合.m3u8"),
                utf8("#EXTM3U\n#EXTINF:215,周杰倫 - 七里香\n..\\音樂\\七里香.flac\n\n"
                     "#EXTINF:-1,沒有長度\nfile:///E:/%E9%9F%B3%E6%A8%82/a%20b.flac\n"
                     "  E:/音樂/c.flac  \n"));
    f.importer.importFiles({list});
    CHECK(f.paths(f.lastId()) == QStringList{f.native(QStringLiteral("音樂/七里香.flac")),
                                             QStringLiteral("E:\\音樂\\a b.flac"),
                                             QStringLiteral("E:\\音樂\\c.flac")});
    CHECK(f.store.playlist(f.lastId()).value()->name == QStringLiteral("混合"));
}

TEST_CASE("AC-12-2：pls 的 File<n>＝／Title<n>＝ 亂序時依 n 排序") {
    Fixture f;
    f.everythingExists = true;
    const QString list =
        f.write(QStringLiteral("亂序.pls"),
                utf8("[playlist]\r\nTitle3=三\r\nFile3=c.flac\r\nfile1=E:\\音樂\\a.flac\r\n"
                     "Title1=一\r\nLength1=200\r\nFILE10=j.flac\r\nFile2=file:///E:/b.flac\r\n"
                     "NumberOfEntries=4\r\nVersion=2\r\n"));
    f.importer.importFiles({list});
    CHECK(f.paths(f.lastId()) ==
          QStringList{QStringLiteral("E:\\音樂\\a.flac"), QStringLiteral("E:\\b.flac"),
                      f.native(QStringLiteral("c.flac")), f.native(QStringLiteral("j.flac"))});
}
