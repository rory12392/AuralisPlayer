// 全螢幕底色（T-69，AC-17-2）：封面主色壓暗（HSL 明度 ≤ 0.10、飽和度 ≤ 0.40，沒有封面用 #050505），
// 任何輸入色和 fsInkMuted 的對比 ≥ 4.5:1（DESIGN §2.3）；在 JobRunner 工作裡取主色，
// 快速連續切歌只有最後一首生效；Lyrics.fullscreenBackdrop 跟著變並發出通知。
#include "backdrop.h"
#include "fake_playback_service.h"
#include "lyr_test_helpers.h"
#include "lyrics_loader.h"
#include "lyrics_view.h"

#include <auralis/cover_service.h>
#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

/// 數訊號發出幾次（這個測試執行檔不連結 Qt Test，沒有 QSignalSpy）。
template <typename Sender, typename Signal>
std::shared_ptr<int> countSignal(Sender* sender, Signal signal) {
    auto count = std::make_shared<int>(0);
    QObject::connect(sender, signal, [count] { ++*count; });
    return count;
}

/// DESIGN §2.2 的 fsInkMuted（Void 的 inkMuted）。
constexpr quint32 kFsInkMuted = 0x9a968e;

double channel(quint32 rgb, int shift) {
    const double c = static_cast<double>((rgb >> shift) & 0xff) / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

/// WCAG 2 的相對亮度與對比。
double luminance(quint32 rgb) {
    return 0.2126 * channel(rgb, 16) + 0.7152 * channel(rgb, 8) + 0.0722 * channel(rgb, 0);
}

double contrast(quint32 a, quint32 b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

quint32 parse(const QString& hex) {
    REQUIRE(hex.size() == 7);
    REQUIRE(hex.startsWith(u'#'));
    bool ok = false;
    const quint32 value = hex.mid(1).toUInt(&ok, 16);
    REQUIRE(ok);
    return value;
}

/// HSL 的明度與飽和度（0–1），用來檢查壓暗的上限。
std::pair<double, double> lightnessSaturation(quint32 rgb) {
    const double r = ((rgb >> 16) & 0xff) / 255.0;
    const double g = ((rgb >> 8) & 0xff) / 255.0;
    const double b = (rgb & 0xff) / 255.0;
    const double hi = std::max({r, g, b});
    const double lo = std::min({r, g, b});
    const double l = (hi + lo) / 2;
    const double d = hi - lo;
    const double s = d == 0 ? 0 : d / (1 - std::abs(2 * l - 1));
    return {l, s};
}

/// 記憶體裡的封面服務：每個 key 一個主色（nullopt＝讀不到），可以變慢或丟例外。
class FakeCovers final : public CoverService {
public:
    FakeCovers() = default;
    void set(const QString& key, std::optional<quint32> color, int delayMs = 0) {
        std::lock_guard lock(m_mutex);
        m_colors.insert(key, color);
        m_delay.insert(key, delayMs);
    }
    void setThrows(const QString& key) {
        std::lock_guard lock(m_mutex);
        m_throws.insert(key, true);
    }
    std::optional<quint32> dominantColor(const QString& key) override {
        int delay = 0;
        std::optional<quint32> color;
        bool boom = false;
        {
            std::lock_guard lock(m_mutex);
            delay = m_delay.value(key);
            color = m_colors.value(key);
            boom = m_throws.value(key);
        }
        QThread::msleep(static_cast<unsigned long>(delay));
        if (boom) {
            throw std::runtime_error("故障注入");
        }
        return color;
    }
    Result<QString> exportThumbnail(const QString&, int) override {
        return Error{u"不支援"_s, 0, u"exportThumbnail"_s};
    }

private:
    std::mutex m_mutex;
    QHash<QString, std::optional<quint32>> m_colors;
    QHash<QString, int> m_delay;
    QHash<QString, bool> m_throws;
};

struct BackdropFixture {
    bool app = (ensureApp(), true); // JobRunner 之前要有 QCoreApplication
    LyrLibrary library;
    FakeCovers covers;
    JobRunner jobs;
    BackdropLoader loader{jobs, library, covers};

    /// 一首曲目（路徑＝名稱），封面 key＝名稱、主色 color。
    TrackRef track(const QString& name, std::optional<quint32> color, int delayMs = 0) {
        library.addCoverTrack(name, name);
        covers.set(name, color, delayMs);
        return {name, 0};
    }
};

} // namespace

TEST_CASE("AC-17-2：任何輸入色壓暗後與 fsInkMuted 的對比 ≥ 4.5:1，明度 ≤ 0.10、飽和度 ≤ 0.40") {
    // DESIGN 指定的六種，再加上 RGB 每軸 16 階的所有組合（含最亮、最飽和的邊界）
    QList<quint32> inputs{0xffffff, 0xffff00, 0x0000ff, 0x808080, 0x000000, 0xff0000};
    for (quint32 r = 0; r < 256; r += 17) {
        for (quint32 g = 0; g < 256; g += 17) {
            for (quint32 b = 0; b < 256; b += 17) {
                inputs.append((r << 16) | (g << 8) | b);
            }
        }
    }
    double worst = 100;
    for (const quint32 input : inputs) {
        const quint32 out = parse(backdropColor(input));
        worst = std::min(worst, contrast(out, kFsInkMuted));
        const auto [l, s] = lightnessSaturation(out);
        INFO("輸入 " << QString::number(input, 16).toStdString() << " → "
                     << backdropColor(input).toStdString());
        CHECK(contrast(out, kFsInkMuted) >= 4.5);
        CHECK(l <= 0.10 + 0.5 / 255); // 量化到 8 位元的誤差
        // 很暗的顏色量化後飽和度會跳動（差 1 階就算很大），只檢查明度夠的
        if (l >= 0.05) {
            CHECK(s <= 0.40 + 0.05);
        }
    }
    INFO("最差對比 " << worst);
    CHECK(worst >= 4.5);
}

TEST_CASE("AC-17-2：色相保留，暗色不提亮；沒有封面或取色失敗是 #050505") {
    CHECK(backdropColor(std::nullopt) == u"#050505"_s);
    // 純紅：明度 0.5、飽和度 1 → 0.10、0.40：R = 0.14、G = B = 0.06
    CHECK(backdropColor(0xff0000) == u"#240f0f"_s);
    // 已經夠暗的顏色照原樣（不提亮）
    CHECK(backdropColor(0x101418) == u"#101418"_s);
    CHECK(backdropColor(0x000000) == u"#000000"_s);
}

TEST_CASE("AC-17-2：在背景工作取主色，回到 UI 執行緒後才變；工作名稱給人看") {
    BackdropFixture f;
    QStringList submitted;
    QObject::connect(&f.jobs, &JobRunner::submitted,
                     [&submitted](quint64, const QString& name) { submitted.append(name); });
    const auto changed = countSignal(&f.loader, &BackdropLoader::changed);
    CHECK(f.loader.color() == u"#050505"_s);
    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(submitted.size() == 1);
    CHECK(submitted.first() == u"計算封面主色"_s);
    CHECK(f.loader.color() == u"#050505"_s); // 結果回來之前維持目前的顏色
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));
    CHECK(*changed == 1);

    // 同色的下一首（同一張專輯）不發通知
    f.loader.load(f.track(u"red2"_s, 0xff0000));
    REQUIRE(f.jobs.waitForDone(5000));
    waitFor([] { return false; }, 100);
    CHECK(*changed == 1);
}

TEST_CASE("AC-17-2：沒有封面、讀不到、工作丟例外時用 #050505；clear 也是") {
    BackdropFixture f;
    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));

    f.loader.load(f.track(u"unreadable"_s, std::nullopt));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#050505"_s; }));

    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));
    f.library.addCoverTrack(u"nocover"_s, QString()); // coverKey 空字串＝沒有封面
    f.loader.load({u"nocover"_s, 0});
    REQUIRE(waitFor([&] { return f.loader.color() == u"#050505"_s; }));

    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));
    f.loader.load({u"notinlibrary"_s, 0}); // 不在音樂庫的檔案
    REQUIRE(waitFor([&] { return f.loader.color() == u"#050505"_s; }));

    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));
    f.covers.setThrows(u"boom"_s);
    f.library.addCoverTrack(u"boom"_s, u"boom"_s);
    f.loader.load({u"boom"_s, 0});
    REQUIRE(waitFor([&] { return f.loader.color() == u"#050505"_s; }));

    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return f.loader.color() == u"#240f0f"_s; }));
    f.loader.clear();
    CHECK(f.loader.color() == u"#050505"_s);
}

TEST_CASE("快速連續切歌：只有最後一首的主色生效，舊的結果晚到也丟掉") {
    BackdropFixture f;
    const auto changed = countSignal(&f.loader, &BackdropLoader::changed);
    // 慢的先排、快的後排：慢的晚到
    f.loader.load(f.track(u"slowRed"_s, 0xff0000, 300));
    QThread::msleep(30);
    f.loader.load(f.track(u"fastBlue"_s, 0x0000ff));
    REQUIRE(f.jobs.waitForDone(5000));
    waitFor([] { return false; }, 200);
    CHECK(f.loader.color() == backdropColor(0x0000ff));
    CHECK(*changed == 1); // 中間沒有先變紅

    // 快的先排、慢的後排：快的先回來，但已經不是最後一首
    f.loader.load(f.track(u"fastGreen"_s, 0x00ff00));
    f.loader.load(f.track(u"slowYellow"_s, 0xffff00, 200));
    REQUIRE(f.jobs.waitForDone(5000));
    waitFor([] { return false; }, 200);
    CHECK(f.loader.color() == backdropColor(0xffff00));
    CHECK(*changed == 2);

    // 切歌後馬上 clear：晚到的結果不覆蓋 #050505
    f.loader.load(f.track(u"slowRed"_s, 0xff0000, 200));
    f.loader.clear();
    REQUIRE(f.jobs.waitForDone(5000));
    waitFor([] { return false; }, 200);
    CHECK(f.loader.color() == u"#050505"_s);
}

TEST_CASE("載入器先被刪掉時，還在跑的取色工作不會把結果送給已刪掉的物件") {
    ensureApp();
    LyrLibrary library;
    FakeCovers covers;
    JobRunner jobs;
    library.addCoverTrack(u"late"_s, u"late"_s);
    covers.set(u"late"_s, 0xff0000, 200);
    {
        BackdropLoader loader(jobs, library, covers);
        loader.load({u"late"_s, 0});
        QThread::msleep(50);
    }
    REQUIRE(jobs.waitForDone(5000));
    waitFor([] { return false; }, 100);
    SUCCEED("沒有當掉");
}

// P-LYR-10：模組停止後核心才解構音樂庫與封面服務，用 LyrLibrary::gone 替身旗標表示「已解構」。
TEST_CASE("AC-17-2（P-LYR-10）：停止時進行中的取色等它結束，還在排隊的之後開始也不碰核心服務") {
    ensureApp();
    LyrLibrary library;
    FakeCovers covers;
    JobRunner jobs(1); // 一條執行緒：第一首在算時第二首只能排隊
    BackdropLoader loader(jobs, library, covers);
    library.addCoverTrack(u"busy"_s, u"busy"_s);
    covers.set(u"busy"_s, 0xff0000, 300);
    loader.load({u"busy"_s, 0});
    QThread::msleep(50); // 讓第一首進到取色（佔住執行緒 300 ms）
    // 換歌會取消第一首，但取色不能中途停，它照樣跑完；第二首排在後面
    loader.load({u"queued"_s, 0});
    QElapsedTimer elapsed;
    elapsed.start();
    loader.shutdown(7000);
    const qint64 ms = elapsed.elapsed();
    library.gone = true;
    CHECK(ms >= 150); // 等了第一首剩下的取色時間
    CHECK(ms < 1000);
    REQUIRE(jobs.waitForDone(10000));
    CHECK(library.usedAfterGone.load() == 0); // 第二首開始時直接結束，沒查音樂庫
}

TEST_CASE("AC-17-2：Lyrics.fullscreenBackdrop 跟著底色變並發出通知；沒有接底色時是 #050505") {
    BackdropFixture f;
    LyrDatabase db;
    FakePlaybackService playback;
    LyricsLoader lyrics(f.jobs, f.library, *db.db);
    LyricsView view(lyrics, playback, nullptr);
    CHECK(view.fullscreenBackdrop() == u"#050505"_s);

    view.setBackdrop(&f.loader);
    const auto notified = countSignal(&view, &LyricsView::fullscreenBackdropChanged);
    f.loader.load(f.track(u"red"_s, 0xff0000));
    REQUIRE(waitFor([&] { return view.fullscreenBackdrop() == u"#240f0f"_s; }));
    CHECK(*notified == 1);
}
