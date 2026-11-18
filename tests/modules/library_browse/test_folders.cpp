// 資料夾頁（S-06）的 C++ 部分（T-23，US-10 AC-3）：資料夾樹與磁碟一致（AC-10-3）、展開收合、
// 含子資料夾的曲目、找不到的資料夾、左欄寬度的保存、讀取失敗。
#include "fake_playback_service.h"
#include "folders_controller.h"
#include "lib_memory_library.h"
#include "lib_test_helpers.h"
#include "library_fixtures.h"

#include <QDir>
#include <QHash>

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

TrackInfo track(const QString& path, qint64 folderId) {
    return memTrack(0, path, QStringLiteral("x"), folderId);
}

FolderInfo folder(qint64 id, qint64 parentId, const QString& path) {
    return memFolder(id, parentId, path);
}

QStringList visibleNames(const FoldersModel& model) {
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        names.append(model.data(model.index(row), FoldersModel::NameRole).toString());
    }
    return names;
}

} // namespace

TEST_CASE("AC-10-3：資料夾樹與磁碟上的資料夾結構一致（全形字、深層資料夾）") {
    ensureApp();
    const QString dir = freshTestDir(QStringLiteral("t-23-tree"));
    const QString root = dir + QStringLiteral("\\音樂");
    buildSampleLibrary(root);
    writeFlac(root + QStringLiteral("\\01 ＡＣＧ\\01 Anime\\作品\\01.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("深層")}});
    const QString dbPath = dir + QStringLiteral("\\library.db");
    scanIntoDatabase(dbPath, root);
    core::LibraryQueryImpl library(dbPath); // 只保存路徑的參考

    QList<FolderNode> nodes;
    REQUIRE(readFolderTree(library, nodes, true).isEmpty());
    REQUIRE_FALSE(nodes.isEmpty());
    CHECK(nodes.at(0).depth == 0);
    CHECK(QDir::cleanPath(nodes.at(0).info.path) == QDir::cleanPath(root));

    QStringList relative;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        const FolderNode& node = nodes.at(i);
        INFO(node.info.path.toStdString());
        CHECK(QFileInfo(node.info.path).isDir()); // 每個節點都是磁碟上真的資料夾
        CHECK_FALSE(node.missing);
        if (node.parent >= 0) {
            const FolderNode& parent = nodes.at(node.parent);
            CHECK(node.depth == parent.depth + 1);
            // 父子關係和路徑一致
            CHECK(QDir::cleanPath(node.info.path) ==
                  QDir::cleanPath(parent.info.path + QLatin1Char('/') + node.info.name));
            CHECK(i > node.parent);
            CHECK(i < parent.subtreeEnd);
        }
        relative.append(QDir(root).relativeFilePath(node.info.path));
    }
    // 有音樂檔的資料夾與它們的上層都在樹裡，順序是前序（父在子前面）
    for (const QString& expected :
         {QStringLiteral("專輯 A"), QStringLiteral("其他"), QStringLiteral("其他/深一層"),
          QStringLiteral("DSD"), QStringLiteral("01 ＡＣＧ"), QStringLiteral("01 ＡＣＧ/01 Anime"),
          QStringLiteral("01 ＡＣＧ/01 Anime/作品")}) {
        INFO(expected.toStdString());
        CHECK(relative.contains(expected));
    }
    CHECK(relative.indexOf(QStringLiteral("其他")) <
          relative.indexOf(QStringLiteral("其他/深一層")));

    // 含子資料夾的曲目：「01 ＡＣＧ」有深層的那一首；根是全部
    const int acg = static_cast<int>(relative.indexOf(QStringLiteral("01 ＡＣＧ")));
    QList<TrackInfo> tracks;
    REQUIRE(readFolderTracks(library, subtreeFolderIds(nodes, acg), tracks).isEmpty());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks.at(0).title == QStringLiteral("深層"));
    REQUIRE(readFolderTracks(library, subtreeFolderIds(nodes, 0), tracks).isEmpty());
    CHECK(tracks.size() == library.tracks(TrackFilter{}).value().size());
}

TEST_CASE("S-06：磁碟上找不到的資料夾標記 missing") {
    ensureApp();
    const QString dir = freshTestDir(QStringLiteral("t-23-missing"));
    const QString root = dir + QStringLiteral("\\音樂");
    writeFlac(root + QStringLiteral("\\留著\\a.flac"), {});
    writeFlac(root + QStringLiteral("\\搬走\\b.flac"), {});
    const QString dbPath = dir + QStringLiteral("\\library.db");
    scanIntoDatabase(dbPath, root);
    // 掃描後才把測試資料夾改名（測試自己產生的暫存資料夾，不是使用者的資料）
    REQUIRE(QDir(root).rename(QStringLiteral("搬走"), QStringLiteral("搬走了")));
    core::LibraryQueryImpl library(dbPath);

    QList<FolderNode> nodes;
    REQUIRE(readFolderTree(library, nodes, true).isEmpty());
    QHash<QString, bool> missing;
    for (const FolderNode& node : nodes) {
        missing.insert(node.info.name, node.missing);
    }
    CHECK(missing.value(QStringLiteral("搬走")) == true);
    CHECK(missing.value(QStringLiteral("留著")) == false);
}

TEST_CASE("S-06：少量資料夾逐一查、大量資料夾讀全部再分，曲目順序相同") {
    ensureApp();
    MemoryLibrary library;
    QList<FolderInfo> folders{folder(1, 0, "C:\\m")};
    QList<TrackInfo> tracks;
    // 根底下 40 個子資料夾（超過逐一查的上限），名稱順序和 id 順序相反
    for (int i = 0; i < 40; ++i) {
        const qint64 id = 100 - i;
        folders.append(folder(id, 1, QStringLiteral("C:\\m\\%1").arg(i, 2, 10, QLatin1Char('0'))));
        tracks.append(track(QStringLiteral("C:/m/%1.flac").arg(i), id));
    }
    tracks.append(track("C:/m/root.flac", 1));
    library.set({}, tracks, folders);

    QList<FolderNode> nodes;
    REQUIRE(readFolderTree(library, nodes, false).isEmpty());
    REQUIRE(nodes.size() == 41);
    const QList<qint64> all = subtreeFolderIds(nodes, 0);
    REQUIRE(all.size() > kPerFolderQueryLimit);

    library.trackCalls = 0;
    QList<TrackInfo> whole;
    REQUIRE(readFolderTracks(library, all, whole).isEmpty());
    CHECK(library.trackCalls == 1); // 大的子樹一次讀完
    REQUIRE(whole.size() == 41);
    CHECK(whole.at(0).ref.path == QStringLiteral("C:/m/root.flac")); // 自己的曲目在子資料夾前面
    CHECK(whole.at(1).ref.path == QStringLiteral("C:/m/0.flac")); // 子資料夾依樹的順序

    library.trackCalls = 0;
    QList<TrackInfo> few;
    REQUIRE(readFolderTracks(library, all.mid(0, 3), few).isEmpty());
    CHECK(library.trackCalls == 3);
    REQUIRE(few.size() == 3);
    for (qsizetype i = 0; i < few.size(); ++i) {
        CHECK(few.at(i).ref.path == whole.at(i).ref.path);
    }
}

TEST_CASE("S-06：樹的展開收合用插入與移除通知，展開狀態在重讀後保留") {
    ensureApp();
    MemoryLibrary library;
    library.set({}, {},
                {folder(1, 0, "C:\\m"), folder(2, 1, "C:\\m\\a"), folder(3, 2, "C:\\m\\a\\x"),
                 folder(4, 1, "C:\\m\\b"), folder(5, 0, "D:\\n")});
    QList<FolderNode> nodes;
    REQUIRE(readFolderTree(library, nodes, false).isEmpty());
    FoldersModel model;
    model.setNodes(nodes);
    // 根預設展開一層
    CHECK(visibleNames(model) == QStringList{"m", "a", "b", "n"});
    CHECK(model.parentRow(1) == 0);
    CHECK(model.parentRow(0) == -1);

    ModelLog log(model);
    CHECK(model.expand(1));
    CHECK(visibleNames(model) == QStringList{"m", "a", "x", "b", "n"});
    CHECK_FALSE(model.expand(1)); // 已展開
    CHECK_FALSE(model.expand(2)); // 沒有子資料夾
    CHECK(model.collapse(0));
    CHECK(visibleNames(model) == QStringList{"m", "n"});
    CHECK_FALSE(model.collapse(1)); // n 沒有子資料夾
    CHECK(log.resets == 0);
    CHECK(log.inserts == 1);
    CHECK(log.removes == 1);

    // 重新展開根：a 仍然是展開的
    CHECK(model.expand(0));
    CHECK(visibleNames(model) == QStringList{"m", "a", "x", "b", "n"});
    model.setNodes(nodes);
    CHECK(visibleNames(model) == QStringList{"m", "a", "x", "b", "n"});
    CHECK(model.rowOfFolder(3) == 2);
}

TEST_CASE("S-06：第一次讀到時音樂庫是空的，掃描後出現的根照樣展開一層") {
    ensureApp();
    FoldersModel model;
    model.setNodes({});
    MemoryLibrary library;
    library.set({}, {}, {folder(1, 0, "C:\\m"), folder(2, 1, "C:\\m\\a")});
    QList<FolderNode> nodes;
    REQUIRE(readFolderTree(library, nodes, false).isEmpty());
    model.setNodes(nodes);
    CHECK(visibleNames(model) == QStringList{"m", "a"});
    CHECK(model.collapse(0));
    model.setNodes(nodes); // 使用者收合過：重讀後不再自動展開
    CHECK(visibleNames(model) == QStringList{"m"});
}

TEST_CASE("S-06：選資料夾讀含子資料夾的曲目，收合時選取改到收合的資料夾，從某首開始播") {
    ensureApp();
    MemoryLibrary library;
    library.set({},
                {track("C:/m/r.flac", 1), track("C:/m/a/1.flac", 2), track("C:/m/a/x/2.flac", 3),
                 track("C:/m/b/3.flac", 4)},
                {folder(1, 0, "C:\\m"), folder(2, 1, "C:\\m\\a"), folder(3, 2, "C:\\m\\a\\x"),
                 folder(4, 1, "C:\\m\\b")});
    FakePlaybackService playback;
    FoldersController folders(library, playback, nullptr, false);
    folders.load();
    REQUIRE(waitFor([&] { return folders.loaded() && folders.tracksLoaded(); }));
    CHECK(folders.selectedRow() == 0); // 第一次選第一個音樂資料夾
    CHECK(folders.selectedPath() == QStringLiteral("C:\\m"));
    CHECK(folders.trackCount() == 4);

    folders.select(1); // a（含 a\x）
    REQUIRE(waitFor([&] { return folders.tracksLoaded(); }));
    CHECK(folders.trackCount() == 2);
    CHECK(folders.playFrom(1));
    CHECK(playback.paths() == QStringList{"C:/m/a/1.flac", "C:/m/a/x/2.flac"});
    CHECK(playback.calls() == QStringList{"playFrom 1"});
    CHECK_FALSE(folders.playFrom(2));

    CHECK(folders.expand(1));
    folders.select(2); // x
    REQUIRE(waitFor([&] { return folders.tracksLoaded(); }));
    CHECK(folders.trackCount() == 1);
    CHECK(folders.collapse(1)); // x 被收進 a：改選 a
    CHECK(folders.selectedRow() == 1);
    REQUIRE(waitFor([&] { return folders.tracksLoaded(); }));
    CHECK(folders.trackCount() == 2);

    // 掃描完成自動重讀，選取保留
    library.emitScanFinished();
    REQUIRE(waitFor([&] { return !folders.busy() && folders.tracksLoaded(); }));
    CHECK(folders.selectedPath() == QStringLiteral("C:\\m\\a"));
    folders.shutdown();
}

TEST_CASE("S-06：左欄寬度預設 320，拖曳調整後存在 LIB/foldersPaneWidth，限制在 200–640") {
    ensureApp();
    MemoryLibrary library;
    FakePlaybackService playback;
    MemorySettings settings;
    {
        FoldersController folders(library, playback, &settings, false);
        CHECK(folders.paneWidth() == 320);
        folders.setPaneWidth(400);
        CHECK(settings.value(QStringLiteral("foldersPaneWidth"), 0).toInt() == 400);
        folders.setPaneWidth(5000);
        CHECK(folders.paneWidth() == 640);
    }
    FoldersController again(library, playback, &settings, false);
    CHECK(again.paneWidth() == 640);
}

TEST_CASE("S-06：讀取失敗顯示錯誤，空的音樂庫沒有資料夾") {
    ensureApp();
    MemoryLibrary library;
    FakePlaybackService playback;
    FoldersController folders(library, playback, nullptr, false);
    folders.load();
    REQUIRE(waitFor([&] { return folders.loaded() && folders.tracksLoaded(); }));
    CHECK(folders.tree()->rowCount() == 0);
    CHECK(folders.selectedRow() == -1);
    CHECK(folders.trackCount() == 0);
    CHECK(folders.error().isEmpty());

    library.failFolders = true;
    folders.reload();
    REQUIRE(waitFor([&] { return !folders.busy(); }));
    CHECK(folders.error() == QStringLiteral("資料庫被鎖住"));
    library.failFolders = false;
    folders.reload(); // 「重試」
    REQUIRE(waitFor([&] { return !folders.busy(); }));
    CHECK(folders.error().isEmpty());
    folders.shutdown();
}
