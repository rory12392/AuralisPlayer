// 歡迎畫面、設定頁的音樂資料夾清單與掃描報告的資料（畫面規格 S-01、S-16、S-17）：掃描進度的文字
// （AC-08-1）、讀不到的檔案進報告且掃描照常完成（AC-08-5）、經 Dialogs 選資料夾與確認移除、
// 加入失敗的通知、每個音樂資料夾的曲目數。用真的 LibraryManager 與測試時產生的小檔。
#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/migrator.h"
#include "shell/library_controller.h"
#include "ui/dialogs_impl.h"

#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

using namespace auralis;
using auralis::core::DialogsImpl;
using auralis::core::LibraryManager;
using auralis::core::LibraryQueryImpl;
using auralis::core::NotifierImpl;
using auralis::ui::LibraryController;

namespace {

/// 測試用的空資料夾：AURALIS_TEST_TMP/t-15/<name>，先清掉上一輪留下的。
QString freshDir(const QString& name) {
    const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-15/") + name;
    QDir(dir).removeRecursively();
    REQUIRE(QDir().mkpath(dir));
    return QDir::toNativeSeparators(dir);
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}

void appendLe(QByteArray& bytes, quint32 value, int size) {
    for (int i = 0; i < size; ++i) {
        bytes.append(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

/// 0.1 秒的 44.1 kHz / 16 bit 立體聲靜音 WAV（標籤讀得到長度與格式就會入庫）。
void writeWav(const QString& path) {
    const quint32 dataSize = 4410 * 4;
    QByteArray bytes("RIFF");
    appendLe(bytes, 36 + dataSize, 4);
    bytes.append("WAVEfmt ");
    appendLe(bytes, 16, 4);
    appendLe(bytes, 1, 2);         // PCM
    appendLe(bytes, 2, 2);         // 聲道
    appendLe(bytes, 44100, 4);     // 取樣率
    appendLe(bytes, 44100 * 4, 4); // 每秒位元組
    appendLe(bytes, 4, 2);         // 區塊大小
    appendLe(bytes, 16, 2);        // 位元深度
    bytes.append("data");
    appendLe(bytes, dataSize, 4);
    bytes.append(QByteArray(static_cast<qsizetype>(dataSize), '\0'));
    writeFile(path, bytes);
}

struct Fixture {
    QString dir;
    QString databasePath;
    JobRunner jobs;
    LibraryQueryImpl query{databasePath};
    LibraryManager manager{jobs, query, databasePath};
    DialogsImpl dialogs;
    NotifierImpl notifier;
    LibraryController library{manager, query, &dialogs, &notifier};
    /// Dialogs 送出的最後一個請求。
    int requestId = 0;
    QString requestKind;
    QVariantMap requestOptions;

    explicit Fixture(const QString& name) {
        dir = freshDir(name);
        databasePath = dir + QStringLiteral("\\library.db");
        REQUIRE(core::migrateDatabase(databasePath).hasValue());
        QObject::connect(&dialogs, &DialogsImpl::requested,
                         [this](int id, const QString& kind, const QVariantMap& options) {
                             requestId = id;
                             requestKind = kind;
                             requestOptions = options;
                         });
    }

    void waitIdle() {
        REQUIRE(QTest::qWaitFor([this] { return !manager.isScanning(); }, 60000));
        library.waitForCounts();
    }
};

} // namespace

TEST_CASE("AC-08-1：已掃描數／總數的寫法（千分位），總數還不知道時不寫") {
    CHECK(LibraryController::formatProgress(12480, 40112) == QStringLiteral("12,480 / 40,112"));
    CHECK(LibraryController::formatProgress(0, 3) == QStringLiteral("0 / 3"));
    CHECK(LibraryController::formatProgress(0, 0).isEmpty());
}

TEST_CASE("AC-08-1：加入資料夾後在背景掃描，掃描完報告與資料夾的曲目數更新") {
    Fixture fx(QStringLiteral("controller-add"));
    const QString music = fx.dir + QStringLiteral("\\音樂");
    for (int i = 0; i < 3; ++i) {
        writeWav(music + QStringLiteral("\\%1.wav").arg(i));
    }
    QSignalSpy scanning(&fx.library, &LibraryController::scanningChanged);
    QSignalSpy report(&fx.library, &LibraryController::reportChanged);

    CHECK_FALSE(fx.library.hasFolders());
    CHECK(fx.library.addFolder(music));
    // 加入後立刻回傳、在背景掃描；歡迎畫面顯示「正在掃描 <資料夾路徑>」
    CHECK(fx.library.scanning());
    CHECK(fx.library.addedPath() == music);
    CHECK(fx.library.hasFolders());
    fx.waitIdle();

    CHECK_FALSE(fx.library.scanning());
    CHECK(scanning.count() >= 2);
    CHECK(report.count() >= 1);
    const QVariantMap summary = fx.library.report();
    CHECK(summary.value(QStringLiteral("hasReport")).toBool());
    CHECK(summary.value(QStringLiteral("added")).toInt() == 3);
    CHECK(summary.value(QStringLiteral("failed")).toInt() == 0);
    CHECK(summary.value(QStringLiteral("tracks")).toInt() == 3);
    CHECK_FALSE(summary.value(QStringLiteral("finishedText")).toString().isEmpty());
    CHECK_FALSE(summary.value(QStringLiteral("durationValue")).toString().isEmpty());
    const QVariantList folders = fx.library.folders();
    REQUIRE(folders.size() == 1);
    CHECK(folders.at(0).toMap().value(QStringLiteral("path")).toString() == music);
    CHECK(folders.at(0).toMap().value(QStringLiteral("trackCount")).toInt() == 3);
    CHECK(fx.library.trackCount() == 3);
    // 沒有問題時清單是空的（畫面顯示「所有檔案都讀取成功。」）
    CHECK(summary.value(QStringLiteral("failures")).toList().isEmpty());
}

TEST_CASE("AC-08-5：讀不到的檔案記在掃描報告（路徑與原因），不中斷掃描") {
    Fixture fx(QStringLiteral("controller-failures"));
    const QString music = fx.dir + QStringLiteral("\\音樂");
    writeWav(music + QStringLiteral("\\a.wav"));
    writeWav(music + QStringLiteral("\\子資料夾\\b.wav"));
    const QString broken = music + QStringLiteral("\\壞掉.flac");
    writeFile(broken, QByteArray("這不是 FLAC"));

    REQUIRE(fx.library.addFolder(music));
    fx.waitIdle();

    const QVariantMap summary = fx.library.report();
    CHECK(summary.value(QStringLiteral("tracks")).toInt() == 2);
    CHECK(summary.value(QStringLiteral("failed")).toInt() == 1);
    const QVariantList failures = summary.value(QStringLiteral("failures")).toList();
    REQUIRE(failures.size() == 1);
    CHECK(failures.at(0).toMap().value(QStringLiteral("path")).toString() == broken);
    CHECK(failures.at(0).toMap().value(QStringLiteral("reason")).toString() ==
          QStringLiteral("檔案損毀"));
    CHECK(fx.library.failuresText() == broken + QStringLiteral("\t檔案損毀"));
}

TEST_CASE("AC-08-5：掃描報告的原因分成沒有讀取權限、檔案損毀，不顯示錯誤碼") {
    CHECK(LibraryController::failureReason(QStringLiteral(
              "無法開啟檔案：HRESULT 0x80070005：存取被拒。")) == QStringLiteral("沒有讀取權限"));
    CHECK(LibraryController::failureReason(QStringLiteral(
              "HRESULT 0x80070020：程序無法存取檔案，因為檔案正由另一個程序使用。")) ==
          QStringLiteral("沒有讀取權限"));
    CHECK(LibraryController::failureReason(QStringLiteral(
              "無法解析檔案（可能已損毀或不是這個格式）")) == QStringLiteral("檔案損毀"));
    CHECK(LibraryController::failureReason(QStringLiteral("HRESULT 0x80070015：裝置未就緒。")) ==
          QStringLiteral("無法讀取"));
    CHECK(LibraryController::failureReason(QStringLiteral("讀不到 CUE 的音訊檔 a.wav：找不到")) ==
          QStringLiteral("讀不到 CUE 的音訊檔 a.wav：找不到"));
}

TEST_CASE("S-01：「加入音樂資料夾」經 Dialogs 選資料夾；取消時不加入、讀不到時通知") {
    Fixture fx(QStringLiteral("controller-choose"));
    const QString music = fx.dir + QStringLiteral("\\音樂");
    writeWav(music + QStringLiteral("\\a.wav"));

    fx.library.chooseFolder();
    CHECK(fx.requestKind == QStringLiteral("folder"));
    fx.dialogs.respond(fx.requestId, QVariantList{});
    CHECK_FALSE(fx.library.hasFolders());

    fx.library.chooseFolder();
    fx.dialogs.respond(fx.requestId, QVariantList{QUrl::fromLocalFile(music)});
    CHECK(fx.library.hasFolders());
    CHECK(fx.library.addedPath() == music);
    fx.waitIdle();

    // 和已加入的互相包含、不存在：不加入，通知列說明
    CHECK_FALSE(fx.library.addFolder(music + QStringLiteral("\\子")));
    CHECK(fx.notifier.current().value(QStringLiteral("kind")) == QStringLiteral("error"));
    const QString missing = fx.dir + QStringLiteral("\\不存在");
    CHECK_FALSE(fx.library.addFolder(missing));
    CHECK(fx.notifier.current().value(QStringLiteral("text")).toString() ==
          QStringLiteral("無法讀取 %1，請確認資料夾存在而且有權限。").arg(missing));
    CHECK(fx.library.folders().size() == 1);
}

TEST_CASE("S-16：移除音樂資料夾先確認，取消時保留、確認後從音樂庫移除") {
    Fixture fx(QStringLiteral("controller-remove"));
    const QString music = fx.dir + QStringLiteral("\\音樂");
    writeWav(music + QStringLiteral("\\a.wav"));
    REQUIRE(fx.library.addFolder(music));
    fx.waitIdle();

    fx.library.confirmRemove(music);
    REQUIRE(fx.requestKind == QStringLiteral("confirm"));
    CHECK(fx.requestOptions.value(QStringLiteral("title")).toString() ==
          QStringLiteral("移除音樂資料夾 %1").arg(music));
    CHECK(fx.requestOptions.value(QStringLiteral("message")).toString() ==
          QStringLiteral("這個資料夾的曲目會從音樂庫消失，音樂檔不會被刪除。"));
    CHECK(fx.requestOptions.value(QStringLiteral("confirmText")).toString() ==
          QStringLiteral("移除資料夾"));
    fx.dialogs.respond(fx.requestId, false);
    CHECK(fx.library.folders().size() == 1);

    fx.library.confirmRemove(music);
    fx.dialogs.respond(fx.requestId, true);
    fx.waitIdle();
    CHECK_FALSE(fx.library.hasFolders());
    CHECK(fx.library.trackCount() == 0);
    // 剛加入的資料夾移除後，歡迎畫面回到初始狀態
    CHECK(fx.library.addedPath().isEmpty());
    // 音樂檔還在（只從音樂庫移除）
    CHECK(QFile::exists(music + QStringLiteral("\\a.wav")));
}
