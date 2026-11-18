// 匯出的流程（T-54，US-14 AC-3；畫面規格 S-10）：儲存對話框的預設檔名、路徑模式記在 PL/、
// 目標已存在先確認（取消就不寫）、完成與失敗的通知、CUE 曲目的提醒。
// 沒給 JobRunner，在呼叫端的執行緒直接做。
#include "pl_export_test_helpers.h"
#include "playlist_exporter.h"

#include <QStandardPaths>

using auralis::Notifier;
using auralis::mod::pl::PlaylistExporter;
using auralis::mod::pl::PlaylistItem;
using auralis::test::ExportDialogs;
using auralis::test::ExportLibrary;
using auralis::test::MapSettings;
using auralis::test::readAll;
using auralis::test::sha256;
using auralis::test::TextNotifier;

namespace {

struct ExportFixture {
    auralis::test::PlDatabase db;
    auralis::mod::pl::PlaylistStore store{*db.db};
    ExportDialogs dialogs;
    TextNotifier notifier;
    MapSettings settings;
    ExportLibrary library;
    PlaylistExporter exporter{store, dialogs, notifier, &settings, &library, nullptr};
    QDir temp{db.dir.path()};
    QStringList finished;
    qint64 id = 0;
    QString track;

    ExportFixture() {
        auralis::test::ensureImportApp();
        QObject::connect(
            &exporter, &PlaylistExporter::finished, [this](const QString& target, bool ok) {
                finished.append(target + (ok ? QStringLiteral(" ok") : QStringLiteral(" 失敗")));
            });
        track = QDir::toNativeSeparators(temp.filePath(QStringLiteral("音樂/a.flac")));
        library.add(track, 0, QStringLiteral("藝人"), QStringLiteral("曲名"), 100'000);
        const QList<PlaylistItem> items{{0, {track, 0}, false}};
        id = store.createImported(QStringLiteral("我的/清單"), items).value().id;
    }
    QString native(const QString& relative) const {
        return QDir::toNativeSeparators(temp.filePath(relative));
    }
    /// 使用者在儲存對話框選了 path（空的＝取消）。
    void choose(const QString& path) {
        REQUIRE(dialogs.pendingSave);
        auto done = std::move(dialogs.pendingSave);
        dialogs.pendingSave = nullptr;
        done(path.isEmpty() ? QStringList() : QStringList{path});
    }
};

} // namespace

TEST_CASE("S-10：open 帶出名稱與上次的路徑模式；選擇位置時記下模式、預設檔名「<名稱>.m3u8」") {
    ExportFixture f;
    f.settings.values.insert(QStringLiteral("exportPathMode"), QStringLiteral("relative"));
    int opened = 0;
    QObject::connect(&f.exporter, &PlaylistExporter::opened, [&opened] { ++opened; });
    f.exporter.open(f.id);
    CHECK(opened == 1);
    CHECK(f.exporter.name() == QStringLiteral("我的/清單"));
    CHECK(f.exporter.relative());

    f.exporter.chooseAndExport(false);
    CHECK(f.settings.values.value(QStringLiteral("exportPathMode")) == QStringLiteral("absolute"));
    // 檔名不能有的字元換成底線；還沒匯出過時從「文件」開始（F-3）
    CHECK(f.dialogs.saveSuggested ==
          QDir::toNativeSeparators(
              QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
              QStringLiteral("/我的_清單.m3u8")));
    CHECK(f.dialogs.saveFilters == QStringList{QStringLiteral("M3U8 播放清單 (*.m3u8)")});
    f.choose(QString());
    CHECK(f.finished.isEmpty());
    CHECK(f.notifier.texts.isEmpty());

    f.exporter.open(f.id);
    CHECK_FALSE(f.exporter.relative());
    f.exporter.open(999);
    CHECK(opened == 2);
}

TEST_CASE("F-3：儲存對話框從上次匯出的資料夾開始（記在 PL/），沒有時用「文件」，不落在音樂資料夾") {
    ExportFixture f;
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(false);
    const QString documents = QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    CHECK(f.dialogs.saveSuggested == documents + QStringLiteral("\\我的_清單.m3u8"));
    // 選了測試暫存的位置：記下資料夾，下一次從那裡開始
    const QString target = f.native(QStringLiteral("匯出/x.m3u8"));
    QDir().mkpath(QFileInfo(target).absolutePath());
    f.choose(target);
    CHECK(f.settings.values.value(QStringLiteral("lastExportFolder")).toString() ==
          f.native(QStringLiteral("匯出")));
    f.exporter.chooseAndExport(false);
    CHECK(f.dialogs.saveSuggested == f.native(QStringLiteral("匯出\\我的_清單.m3u8")));
    f.choose(QString());
}

TEST_CASE("AC-14-3：目標不存在時直接寫，完成通知「已匯出到 <路徑>」") {
    ExportFixture f;
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(true);
    const QString target = f.native(QStringLiteral("清單.m3u8"));
    f.choose(target);
    CHECK(f.dialogs.confirms.isEmpty());
    CHECK(f.finished == QStringList{target + QStringLiteral(" ok")});
    CHECK(f.notifier.texts == QStringList{QStringLiteral("已匯出到 ") + target});
    CHECK(f.notifier.kinds == QList<Notifier::Kind>{Notifier::Kind::Info});
    CHECK(readAll(target) ==
          QStringLiteral("#EXTM3U\r\n#EXTINF:100,藝人 - 曲名\r\n音樂\\a.flac\r\n").toUtf8());
}

TEST_CASE("AC-14-3（M-04 審查）：匯出的背景工作被取消後忙碌狀態清掉，之後照常能匯出") {
    ExportFixture f;
    auralis::test::HeldJobs held;
    PlaylistExporter exporter{f.store, f.dialogs, f.notifier, &f.settings, &f.library, &held.jobs};
    QStringList finished;
    QObject::connect(
        &exporter, &PlaylistExporter::finished, [&finished](const QString& target, bool ok) {
            finished.append(target + (ok ? QStringLiteral(" ok") : QStringLiteral(" 失敗")));
        });
    exporter.open(f.id);
    exporter.chooseAndExport(true);
    const QString cancelled = f.native(QStringLiteral("取消.m3u8"));
    f.choose(cancelled);
    CHECK(exporter.busy());
    held.cancelAndDrain();
    CHECK_FALSE(exporter.busy());
    CHECK(finished == QStringList{cancelled + QStringLiteral(" 失敗")});
    CHECK(f.notifier.texts.isEmpty());

    held.hold();
    exporter.chooseAndExport(true);
    const QString target = f.native(QStringLiteral("之後.m3u8"));
    f.choose(target);
    held.drain();
    CHECK_FALSE(exporter.busy());
    CHECK(finished.last() == target + QStringLiteral(" ok"));
    CHECK(f.notifier.texts.last() == QStringLiteral("已匯出到 ") + target);
}

TEST_CASE("AC-14-3：目標已存在時先確認；取消就不寫，確認才覆蓋") {
    ExportFixture f;
    const QString target = f.native(QStringLiteral("已存在.m3u8"));
    {
        QFile file(target);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("原本的內容");
    }
    const QByteArray before = sha256(target);
    f.exporter.open(f.id);

    f.exporter.chooseAndExport(false);
    f.choose(target);
    REQUIRE(f.dialogs.confirms.size() == 1);
    const auto request = f.dialogs.confirms.first();
    CHECK(request.title == QStringLiteral("覆蓋 已存在.m3u8"));
    CHECK(request.message == QStringLiteral("這個檔案已經存在，覆蓋後無法復原。"));
    CHECK(request.confirmText == QStringLiteral("覆蓋"));
    CHECK(request.destructive);
    f.dialogs.pendingConfirm(false);
    CHECK(sha256(target) == before);
    CHECK(f.finished.isEmpty());
    CHECK(f.notifier.texts.isEmpty());

    f.exporter.chooseAndExport(false);
    f.choose(target);
    REQUIRE(f.dialogs.confirms.size() == 2);
    f.dialogs.pendingConfirm(true);
    CHECK(f.finished == QStringList{target + QStringLiteral(" ok")});
    CHECK(readAll(target).startsWith("#EXTM3U\r\n"));
}

TEST_CASE("S-10：沒有副檔名時補上 .m3u8，已存在的確認看補上之後的檔名") {
    ExportFixture f;
    const QString withSuffix = f.native(QStringLiteral("沒副檔名.m3u8"));
    {
        QFile file(withSuffix);
        REQUIRE(file.open(QIODevice::WriteOnly));
    }
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(false);
    f.choose(f.native(QStringLiteral("沒副檔名")));
    REQUIRE(f.dialogs.confirms.size() == 1);
    CHECK(f.dialogs.confirms.first().title == QStringLiteral("覆蓋 沒副檔名.m3u8"));
}

TEST_CASE("寫使用者檔案：寫不進去時錯誤通知，不顯示完成") {
    ExportFixture f;
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(false);
    const QString target = f.native(QStringLiteral("沒有這個資料夾/清單.m3u8"));
    f.choose(target);
    CHECK(f.finished == QStringList{target + QStringLiteral(" 失敗")});
    CHECK(f.notifier.texts == QStringList{QStringLiteral("無法匯出到 ") + target});
    CHECK(f.notifier.kinds == QList<Notifier::Kind>{Notifier::Kind::Error});
}

TEST_CASE("S-10：有 CUE 曲目時完成通知加「CUE 曲目以整軌檔的路徑匯出」") {
    ExportFixture f;
    const QString cue = f.native(QStringLiteral("音樂/專輯.cue"));
    f.library.add(cue, 2, QStringLiteral("藝人"), QStringLiteral("第二軌"), 1'000);
    f.library.audioOf.insert(cue, f.native(QStringLiteral("音樂/專輯.flac")));
    REQUIRE(f.store.addTracks(f.id, {{cue, 2}}));
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(false);
    const QString target = f.native(QStringLiteral("cue.m3u8"));
    f.choose(target);
    CHECK(f.notifier.texts ==
          QStringList{QStringLiteral("已匯出到 %1。CUE 曲目以整軌檔的路徑匯出").arg(target)});
}

TEST_CASE(
    "M-04 審查（疑點 2）：有路徑含控制字元的項目時完成通知加「<n> 首的路徑含無效字元，沒有匯出」") {
    ExportFixture f;
    REQUIRE(f.store.addTracks(f.id, {{QStringLiteral("http://x/a.flac\r\n\\\\evil\\x.flac"), 0},
                                     {QStringLiteral("C:\\a\r\nC:\\b.flac"), 0}}));
    f.exporter.open(f.id);
    f.exporter.chooseAndExport(false);
    const QString target = f.native(QStringLiteral("無效.m3u8"));
    f.choose(target);
    CHECK(f.notifier.texts ==
          QStringList{QStringLiteral("已匯出到 %1。2 首的路徑含無效字元，沒有匯出").arg(target)});
    CHECK_FALSE(QString::fromUtf8(readAll(target)).contains(QStringLiteral("\n\\\\")));
}
