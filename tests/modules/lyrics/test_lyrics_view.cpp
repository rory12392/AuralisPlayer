// 歌詞面板的資料（單例 Lyrics，T-66）：目前行跟著播放位置（模擬時鐘，誤差 ≤ 100 ms）、
// 跳轉後立刻換行（AC-15-2）、點行跳到該行時間且 offset 已套用（AC-15-3）、
// 沒有歌詞與純文字歌詞的狀態（AC-15-5）。播放是介面測試的假 PlaybackService。
#include "fake_playback_service.h"
#include "lyr_test_helpers.h"
#include "lyrics_loader.h"
#include "lyrics_view.h"

#include <auralis/job_runner.h>
#include <auralis/module_settings.h>

#include <catch2/catch_test_macros.hpp>

#include <QHash>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

class MemorySettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& fallback) const override {
        return m_values.value(key, fallback);
    }
    void setValue(const QString& key, const QVariant& value) override {
        m_values.insert(key, value);
    }
    bool contains(const QString& key) const override { return m_values.contains(key); }
    void remove(const QString& key) override { m_values.remove(key); }

private:
    QHash<QString, QVariant> m_values;
};

/// 一首有內嵌歌詞（lyrics 空字串＝沒有）的曲目、載入器與 Lyrics，載入完才回傳。
struct ViewFixture {
    bool app = (ensureApp(), true); // JobRunner 之前要有 QCoreApplication
    LyrDatabase db;
    std::unique_ptr<QTemporaryDir> dir = lyrTempDir();
    LyrLibrary library;
    JobRunner jobs;
    FakePlaybackService playback;
    LyricsLoader loader{jobs, library, *db.db};
    std::unique_ptr<LyricsView> view;

    explicit ViewFixture(const QString& lyrics, ModuleSettings* settings = nullptr) {
        view = std::make_unique<LyricsView>(loader, playback, settings);
        const QString audio = dir->filePath(u"a.flac"_s);
        writeFile(audio, "audio");
        library.setEmbedded(audio, lyrics);
        loader.load({audio, 0});
        REQUIRE(waitFor([&] { return !loader.isLoading(); }));
    }
};

// 時間刻意不整齊（含 300 ms、750 ms 的短間隔），不剛好落在 50 ms 的通知上
const QString kSynced = u"[00:01.00]第一行\n[00:01.30]第二行\n[00:02.05]第三行\n"
                        u"[00:02.80]第四行\n[00:05.17]第五行\n[00:07.43]第六行"_s;

} // namespace

TEST_CASE("AC-15-2：模擬時鐘下目前行和播放位置的誤差 ≤ 100 毫秒") {
    ViewFixture f(kSynced);
    REQUIRE(f.view->state() == u"synced"_s);
    REQUIRE(f.view->lines().size() == 6);
    const LyricsDocument& doc = f.loader.document();

    // 模擬時鐘：每 1 ms 檢查一次，服務照實際行為每 50 ms 送一次位置
    qint64 worstLag = 0;
    for (qint64 now = 0; now <= 9000; ++now) {
        if (now % 50 == 0) {
            f.playback.tick(now);
        }
        // 顯示的行要等於 [now - 100, now] 之間某個位置的正確行；找最小的延遲
        qint64 lag = -1;
        for (qint64 d = 0; d <= 100; ++d) {
            if (lineIndexAt(doc, now - d) == f.view->currentLine()) {
                lag = d;
                break;
            }
        }
        INFO("模擬時間 " << now << " ms，顯示第 " << f.view->currentLine() << " 行");
        REQUIRE(lag >= 0);
        worstLag = std::max(worstLag, lag);
    }
    INFO("最大延遲 " << worstLag << " ms");
    CHECK(worstLag < 50); // 通知間隔 50 ms，延遲不會超過一個間隔
}

TEST_CASE("AC-15-2：跳轉後立刻換到對應行，不等下一次位置通知") {
    ViewFixture f(kSynced);
    f.playback.tick(1100);
    CHECK(f.view->currentLine() == 0);
    int changes = 0;
    QObject::connect(f.view.get(), &LyricsView::currentLineChanged, [&] { ++changes; });

    f.playback.seek(6000); // 拖曳進度列放開：服務立刻回報目標位置
    CHECK(f.view->currentLine() == 4);
    f.playback.seek(1400); // 往回拖
    CHECK(f.view->currentLine() == 1);
    f.playback.seek(500); // 第一行之前
    CHECK(f.view->currentLine() == -1);
    CHECK(changes == 3);
}

TEST_CASE("AC-15-3：點一行跳到該行時間，[offset:] 已套用") {
    ViewFixture f(u"[offset:+500]\n[00:10.00]甲\n[00:20.00]乙"_s);
    REQUIRE(f.view->state() == u"synced"_s);
    f.view->seekToLine(1);
    CHECK(f.playback.positionMs() == 19500); // 正 offset＝歌詞提前
    CHECK(f.view->currentLine() == 1);
    f.view->seekToLine(0);
    CHECK(f.playback.positionMs() == 9500);

    const QStringList before = f.playback.calls();
    f.view->seekToLine(2); // 超出範圍
    f.view->seekToLine(-1);
    CHECK(f.playback.calls() == before);
}

TEST_CASE("AC-15-5：沒有歌詞時狀態是 none，不是空白的清單") {
    MemorySettings settings;
    SECTION("線上抓取開著") {
        ViewFixture f(QString(), &settings);
        CHECK(f.view->state() == u"none"_s);
        CHECK(f.view->lines().isEmpty());
        CHECK(f.view->source().isEmpty());
        CHECK(f.view->onlineEnabled());
    }
    SECTION("線上抓取關閉：面板多一句說明") {
        settings.setValue(u"online"_s, false);
        ViewFixture f(QString(), &settings);
        CHECK(f.view->state() == u"none"_s);
        CHECK_FALSE(f.view->onlineEnabled());
    }
}

TEST_CASE("AC-15-5：純文字歌詞不高亮、點了不跳；來源是檔案內嵌") {
    ViewFixture f(u"第一段\n第二段"_s);
    CHECK(f.view->state() == u"plain"_s);
    CHECK(f.view->lines() == QStringList{u"第一段"_s, u"第二段"_s});
    CHECK(f.view->source() == u"embedded"_s);
    f.playback.tick(5000);
    CHECK(f.view->currentLine() == -1);
    const QStringList before = f.playback.calls();
    f.view->seekToLine(0);
    CHECK(f.playback.calls() == before);
}

TEST_CASE("AC-15-2：換歌時先是載入中，找完才有歌詞；沒有在聽的曲目時顯示沒有歌詞") {
    ViewFixture f(kSynced);
    const QString slow = f.dir->filePath(u"slow.flac"_s);
    writeFile(slow, "audio");
    f.library.setEmbedded(slow, u"[00:00.50]慢"_s);
    f.library.setDelay(slow, 200);
    f.loader.load({slow, 0});
    CHECK(f.view->state() == u"loading"_s);
    CHECK(f.view->lines().isEmpty());
    CHECK(f.view->currentLine() == -1);
    REQUIRE(waitFor([&] { return f.view->state() == u"synced"_s; }));
    CHECK(f.view->lines() == QStringList{u"慢"_s});

    f.loader.clear();
    CHECK(f.view->state() == u"none"_s);
}
