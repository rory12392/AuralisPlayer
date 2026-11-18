// 模組停止時等自己的背景工作（T-97，P-PL-24、P-LYR-10）：核心的 JobRunner 最後才解構，
// 音樂庫、資料庫、封面服務與模組自己的物件比它早，而被取消的排隊工作照樣會開始執行。
// 每個有背景工作的模組各一項：JobRunner 只有一條執行緒、先被佔住，模組的工作只能排隊；
// 呼叫模組 stop() 用的那個停止方法、把服務標成已解構、放行 → 工作不再碰服務（解構後呼叫 0 次）。
// LIB 沒有送進 JobRunner 的工作（瀏覽頁的執行緒池是控制器的成員，模組解構時就等完），不在這裡。
#include "stop_fakes.h"

#include "backdrop.h"
#include "lyrics_loader.h"
#include "playlist_cue.h"
#include "playlist_exporter.h"
#include "playlist_import.h"
#include "playlist_store.h"
#include "playlists_controller.h"
#include "r128_controller.h"
#include "smtc_refresh.h"

#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

using namespace auralis;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

/// 和各模組 stop() 用的時限相同（LRCLIB 一次請求的逾時 5 秒加餘裕）。
constexpr int kStopTimeoutMs = 7000;

/// 停止之後：服務標成已解構、放行排隊中的工作、等它們跑完，回傳解構後被呼叫幾次。
int afterStop(GoneFlag& flag, Blocker& blocker, JobRunner& jobs) {
    flag.gone = true;
    blocker.release();
    REQUIRE(jobs.waitForDone(10000));
    return flag.usedAfterGone.load();
}

QString queryPayload() {
    TrackDragPayload payload;
    payload.query = uR"({"lib":1,"text":"x"})"_s;
    return QString::fromUtf8(payload.toJson());
}

class NullSink final : public mod::smtc::TrackSink {
public:
    void setTrack(const QString&, const QString&, const QString&,
                  const QByteArray&) noexcept override {}
    void setThumbnail(const QByteArray&) noexcept override {}
    void clearTrack() noexcept override {}
};

} // namespace

TEST_CASE("隔離（P-PL-24）：PL 停止後，排隊中的背景工作之後開始也不碰音樂庫與播放清單資料庫") {
    ensureStopApp();
    GoneFlag flag;
    StopLibrary library(flag);
    StopDatabase database(flag, u"PL"_s);
    mod::pl::PlaylistStore store(database);
    StopDialogs dialogs;
    StopNotifier notifier;
    JobRunner jobs(1);
    mod::pl::PlaylistsController controller(
        store, dialogs, notifier,
        [&library](const QString& query) { return library.tracksForDragQuery(query); }, &jobs);

    SECTION("加入播放清單（依查詢條件取曲目）") {
        Blocker blocker(jobs);
        REQUIRE(controller.createWithTracks(queryPayload()) == -1); // 排進背景工作
        controller.shutdown(kStopTimeoutMs);
        CHECK(afterStop(flag, blocker, jobs) == 0);
    }
    SECTION("重新檢查所有播放清單（匯入與重新對應共用同一個工作入口）") {
        mod::pl::PlaylistImporter importer(store, controller, dialogs, notifier, &jobs, nullptr);
        importer.setLibraryCheck([&library] { return mod::pl::libraryCheck(library); });
        Blocker blocker(jobs);
        importer.recheckAll();
        importer.shutdown(kStopTimeoutMs);
        CHECK(afterStop(flag, blocker, jobs) == 0);
    }
    SECTION("匯入播放清單檔") {
        mod::pl::PlaylistImporter importer(store, controller, dialogs, notifier, &jobs, nullptr);
        importer.setLibraryCheck([&library] { return mod::pl::libraryCheck(library); });
        // 一行整張 .cue：匯入時在工作執行緒向音樂庫查它的軌號（展開成 CUE 曲目）
        QTemporaryDir in(QString::fromUtf8(AURALIS_TEST_TMP) + u"/t-97-import-XXXXXX"_s);
        REQUIRE(in.isValid());
        QFile cue(in.filePath(u"t-97.cue"_s));
        REQUIRE(cue.open(QIODevice::WriteOnly));
        cue.close();
        QFile list(in.filePath(u"t-97.m3u8"_s));
        REQUIRE(list.open(QIODevice::WriteOnly));
        list.write("t-97.cue\n");
        list.close();
        Blocker blocker(jobs);
        importer.importFiles({list.fileName()});
        importer.shutdown(kStopTimeoutMs);
        CHECK(afterStop(flag, blocker, jobs) == 0);
    }
    SECTION("匯出 .m3u8") {
        const auto id = store.create(u"t-97"_s);
        REQUIRE(id.hasValue());
        mod::pl::PlaylistExporter exporter(store, dialogs, notifier, nullptr, &library, &jobs);
        exporter.open(id.value());
        exporter.chooseAndExport(false);
        REQUIRE(dialogs.pendingSave);
        QTemporaryDir out(QString::fromUtf8(AURALIS_TEST_TMP) + u"/t-97-export-XXXXXX"_s);
        REQUIRE(out.isValid());
        Blocker blocker(jobs);
        dialogs.pendingSave({out.filePath(u"t-97.m3u8"_s)});
        exporter.shutdown(kStopTimeoutMs);
        CHECK(afterStop(flag, blocker, jobs) == 0);
    }
}

TEST_CASE("隔離：OUT 停止後，排隊中的 R128 計算之後開始也不碰音樂庫與資料庫") {
    ensureStopApp();
    GoneFlag flag;
    StopLibrary library(flag);
    StopDatabase database(flag, u"OUT"_s);
    JobRunner jobs(1);
    mod::out::R128Controller r128(
        jobs, library, database,
        [](const TrackRef&) -> Result<std::unique_ptr<IAudioSource>> {
            return Error{u"測試不開檔"_s, 0, u"open"_s};
        },
        nullptr);
    Blocker blocker(jobs);
    r128.computeLibrary();
    r128.shutdown(kStopTimeoutMs);
    CHECK(afterStop(flag, blocker, jobs) == 0);
}

TEST_CASE("隔離：SMTC 停止後，排隊中的曲目資訊查詢之後開始也不碰音樂庫與封面服務") {
    ensureStopApp();
    GoneFlag flag;
    StopLibrary library(flag);
    StopCovers covers(flag);
    NullSink sink;
    JobRunner jobs(1);
    // 和模組相同的接法：工作送進 JobRunner，結果排回主執行緒
    auto refresher = std::make_unique<mod::smtc::TrackRefresher>(
        library, covers, sink,
        [&jobs](mod::smtc::TrackRefresher::Job job) {
            jobs.submit(u"SMTC 曲目資訊"_s, [job = std::move(job)](JobContext& context) {
                job([&context] { return context.isCancelled(); });
            });
        },
        [](std::function<void()> function) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(function),
                                      Qt::QueuedConnection);
        });
    Blocker blocker(jobs);
    refresher->refresh(TrackRef{u"D:/音樂/t-97.flac"_s, 0});
    refresher->shutdown(kStopTimeoutMs);
    refresher.reset();
    CHECK(afterStop(flag, blocker, jobs) == 0);
}

TEST_CASE("隔離（P-LYR-10）：LYR 停止後，排隊中的歌詞查詢與封面主色之後開始也不碰核心服務") {
    ensureStopApp();
    GoneFlag flag;
    StopLibrary library(flag);
    StopDatabase database(flag, u"LYR"_s);
    StopCovers covers(flag);
    JobRunner jobs(1);
    mod::lyr::LyricsLoader lyrics(jobs, library, database);
    mod::lyr::BackdropLoader backdrop(jobs, library, covers);
    Blocker blocker(jobs);
    lyrics.load({u"D:/音樂/t-97.flac"_s, 0});
    backdrop.load({u"D:/音樂/t-97.flac"_s, 0});
    lyrics.shutdown(kStopTimeoutMs);
    backdrop.shutdown(kStopTimeoutMs);
    CHECK(afterStop(flag, blocker, jobs) == 0);
}
