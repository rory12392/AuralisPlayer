// 開始播放的入口（AC-11-5）：拖進視窗或命令列給的檔案與資料夾展開成佇列的曲目，不需要先加入音樂庫。
#include "cue_fixtures.h"
#include "library_fixtures.h"

#include "library/open_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

QStringList describe(const QList<TrackRef>& tracks, const QString& root) {
    QStringList result;
    for (const auto& track : tracks) {
        QString relative = track.path.mid(root.size() + 1);
        result.append(track.subsong == 0
                          ? relative
                          : relative + QLatin1Char('#') + QString::number(track.subsong));
    }
    return result;
}

/// 每個檔案的內容雜湊與修改時間（證明展開只讀）。
QMap<QString, QString> snapshot(const QString& root) {
    QMap<QString, QString> result;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        result.insert(
            path, QString::fromLatin1(
                      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex()) +
                      QLatin1Char('|') +
                      QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()));
    }
    return result;
}

/// 人手驗證那一套資料夾的縮小版：數字開頭的子資料夾、CUE 整軌、不收錄的檔案。
QString buildFolder(const QString& name) {
    const QString root = freshTestDir(name);
    writeFlac(root + QStringLiteral("\\10 十\\b.flac"), {});
    writeFlac(root + QStringLiteral("\\2 二\\10.flac"), {});
    writeFlac(root + QStringLiteral("\\2 二\\9.flac"), {});
    writeWav(root + QStringLiteral("\\2 二\\A.wav"), {});
    writeFlac(root + QStringLiteral("\\06 CUE 整軌\\album.flac"), {}, 44100, 16,
              44100 * kSampleAudioSeconds);
    writeBytes(root + QStringLiteral("\\06 CUE 整軌\\album.cue"),
               cueFixture(QStringLiteral("utf8-bom.cue")));
    writeBytes(root + QStringLiteral("\\notes.txt"), "不是音樂");
    writeBytes(root + QStringLiteral("\\1.m3u8"), "#EXTM3U\n");
    return root;
}

} // namespace

TEST_CASE("AC-11-5：資料夾遞迴展開成音樂檔，依路徑自然排序，.cue 展開成各軌、不重複加整軌檔") {
    const QString root = buildFolder(QStringLiteral("t-85-folder"));
    const auto before = snapshot(root);

    const OpenPathsResult result = expandOpenPaths({root});

    // 數字照數值比：2 < 06 < 10、9 < 10
    CHECK(describe(result.tracks, root) ==
          QStringList{QStringLiteral("2 二\\9.flac"), QStringLiteral("2 二\\10.flac"),
                      QStringLiteral("2 二\\A.wav"), QStringLiteral("06 CUE 整軌\\album.cue#1"),
                      QStringLiteral("06 CUE 整軌\\album.cue#2"),
                      QStringLiteral("06 CUE 整軌\\album.cue#3"), QStringLiteral("10 十\\b.flac")});
    CHECK(result.skipped.isEmpty());
    // 只讀：每個檔案的內容與修改時間都沒變
    CHECK(snapshot(root) == before);
}

TEST_CASE("AC-11-5：直接給 .cue 展開成各軌；同時給整軌檔時整軌檔不另外加入") {
    const QString root = buildFolder(QStringLiteral("t-85-cue"));
    const QString cue = root + QStringLiteral("\\06 CUE 整軌\\album.cue");
    const QString whole = root + QStringLiteral("\\06 CUE 整軌\\album.flac");
    const QString single = root + QStringLiteral("\\2 二\\9.flac");

    const OpenPathsResult result = expandOpenPaths({single, whole, cue});

    CHECK(describe(result.tracks, root) == QStringList{QStringLiteral("2 二\\9.flac"),
                                                       QStringLiteral("06 CUE 整軌\\album.cue#1"),
                                                       QStringLiteral("06 CUE 整軌\\album.cue#2"),
                                                       QStringLiteral("06 CUE 整軌\\album.cue#3")});
    CHECK(result.skipped.isEmpty());

    SECTION("只給整軌檔時照整檔播") {
        const OpenPathsResult alone = expandOpenPaths({whole});
        CHECK(describe(alone.tracks, root) ==
              QStringList{QStringLiteral("06 CUE 整軌\\album.flac")});
    }
}

TEST_CASE("AC-11-5：開不了的項目列出原因，其他照常加入（AC-01-3）") {
    const QString root = buildFolder(QStringLiteral("t-85-skip"));
    writeBytes(root + QStringLiteral("\\壞的\\x.cue"), "這不是 CUE");
    const QString good = root + QStringLiteral("\\2 二\\9.flac");

    const OpenPathsResult result = expandOpenPaths({root + QStringLiteral("\\notes.txt"), good,
                                                    root + QStringLiteral("\\不存在.flac"),
                                                    root + QStringLiteral("\\壞的\\x.cue")});

    CHECK(describe(result.tracks, root) == QStringList{QStringLiteral("2 二\\9.flac")});
    REQUIRE(result.skipped.size() == 3);
    CHECK(result.skipped.at(0).path.endsWith(QStringLiteral("notes.txt")));
    CHECK(result.skipped.at(0).error.contains(QStringLiteral("不支援")));
    CHECK(result.skipped.at(1).error.contains(QStringLiteral("找不到")));
    CHECK(result.skipped.at(2).path.endsWith(QStringLiteral("x.cue")));
    CHECK_FALSE(result.skipped.at(2).error.isEmpty());

    SECTION("沒有音樂檔的資料夾：什麼都不加、也不算錯") {
        const QString empty = freshTestDir(QStringLiteral("t-85-empty"));
        writeBytes(empty + QStringLiteral("\\notes.txt"), "x");
        const OpenPathsResult none = expandOpenPaths({empty});
        CHECK(none.tracks.isEmpty());
        CHECK(none.skipped.isEmpty());
    }
}

TEST_CASE("AC-11-5：帶協定前綴、裝置與替代資料流的路徑在查詢檔案系統之前就拒絕") {
    const QStringList rejected{
        QStringLiteral("http://auralis-test.invalid/a.flac"),
        QStringLiteral("https://auralis-test.invalid/a.flac"),
        QStringLiteral("file:C:/a.flac"),
        QStringLiteral("concat:C:\\a.flac|C:\\b.flac"),
        QStringLiteral("smb://host/share/a.flac"),
        QStringLiteral("ab:c.flac"),
        QStringLiteral("\\\\?\\C:\\a.flac"),
        QStringLiteral("\\\\.\\pipe\\x"),
        QStringLiteral("C:\\music\\a.flac:stream"),
        QStringLiteral("C:a.flac"),
        QStringLiteral("C:\\music\\CON"),
        QStringLiteral("C:\\music\\nul.flac"),
        QStringLiteral("a.flac"),
        QString(),
    };
    for (const QString& path : rejected) {
        INFO(path.toStdString());
        CHECK_FALSE(checkUserPath(path).hasValue());
    }
    // 網址開頭的也不展開
    const OpenPathsResult result = expandOpenPaths({rejected.first()});
    CHECK(result.tracks.isEmpty());
    REQUIRE(result.skipped.size() == 1);
    CHECK(result.skipped.first().error.contains(QStringLiteral("只開啟本機檔案")));
}

TEST_CASE("AC-11-5：本機與使用者自己給的網路路徑可以開，相對路徑接在目前資料夾後面") {
    CHECK(checkUserPath(QStringLiteral("C:/音樂/Rё∀L.wav")).value() ==
          QStringLiteral("C:\\音樂\\Rё∀L.wav"));
    CHECK(checkUserPath(QStringLiteral("\\\\nas\\music\\ＮieR：Automata\\a.flac")).value() ==
          QStringLiteral("\\\\nas\\music\\ＮieR：Automata\\a.flac"));
    CHECK(checkUserPath(QStringLiteral("D:\\")).hasValue());

    CHECK(absoluteUserPath(QStringLiteral("a\\b.flac"), QStringLiteral("E:\\x")) ==
          QStringLiteral("E:\\x\\a\\b.flac"));
    CHECK(absoluteUserPath(QStringLiteral("..\\b.flac"), QStringLiteral("E:\\x\\y")) ==
          QStringLiteral("E:\\x\\b.flac"));
    CHECK(absoluteUserPath(QStringLiteral("F:\\b.flac"), QStringLiteral("E:\\x")) ==
          QStringLiteral("F:\\b.flac"));
    CHECK(absoluteUserPath(QStringLiteral("\\\\nas\\m\\b.flac"), QStringLiteral("E:\\x")) ==
          QStringLiteral("\\\\nas\\m\\b.flac"));
    // 協定前綴不能被接成「目前資料夾\http:\…」而躲過檢查
    CHECK_FALSE(
        checkUserPath(absoluteUserPath(QStringLiteral("http://h/a.flac"), QStringLiteral("E:\\x")))
            .hasValue());
}
