// 長時間播放欠載（--self-check --soak）：結果的判定與 JSON、不連續檢查，以及經測試用 DoP 包裝器的
// DSD 部分（--soak-dsd）。PCM 的實際播放由 ctest 的 app.self_check_soak 經主程式跑。
#include "soak.h"

#include "audio/capture_backend.h"
#include "dop_test_sink.h"
#include "ui/ui_registry_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

using auralis::AudioFormat;
using auralis::app::FullscreenSoakWatch;
using auralis::app::runSoak;
using auralis::app::soakErrors;
using auralis::app::soakJson;
using auralis::app::SoakResult;
using auralis::core::ContinuityCheck;

namespace {

/// 播了 5 秒、同格式接續與換格式都有、沒有欠載的結果。
SoakResult cleanResult() {
    SoakResult result;
    result.seconds = 5;
    result.tracks = 3;
    result.formatChanges = 1;
    result.sameFormatJoins = 1;
    result.underruns = 0;
    return result;
}

AudioFormat pcm24() {
    AudioFormat format;
    format.rate = 176400;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    return format;
}

/// 兩個聲道相同的 24-in-32 樣本框。
std::vector<qint32> frames24(std::initializer_list<qint32> values) {
    std::vector<qint32> out;
    for (const qint32 value : values) {
        out.push_back(value);
        out.push_back(value);
    }
    return out;
}

const std::byte* bytesOf(const std::vector<qint32>& samples) {
    return reinterpret_cast<const std::byte*>(samples.data());
}

} // namespace

TEST_CASE("AC-04-5：不連續檢查：PCM 相鄰樣本差超過門檻才算，跨裝置緩衝也接著比") {
    ContinuityCheck check(0.15);
    check.begin(pcm24());
    const auto smooth = frames24({0, 0x08000000, 0x10000000}); // 0、1/16、1/8 滿刻度
    check.feed(bytesOf(smooth), 3);
    CHECK(check.pcmJumps() == 0);
    // 下一個緩衝從 1/8 跳到 1/2：兩個聲道各算一次
    const auto jump = frames24({0x40000000});
    check.feed(bytesOf(jump), 1);
    CHECK(check.pcmJumps() == 2);
    CHECK(check.maxPcmStep() > 0.37);
    // 重開裝置後不和上一次的最後一框比，累計的次數保留
    check.begin(pcm24());
    const auto restart = frames24({0});
    check.feed(bytesOf(restart), 1);
    CHECK(check.pcmJumps() == 2);
}

TEST_CASE("AC-04-6：不連續檢查：DoP 標記要一框一框交替，斷掉的框都算") {
    ContinuityCheck check(0.15);
    check.begin(pcm24());
    // 標記 0x05、0xFA、0xFA（沒交替）、0x05；DSD 資料都一樣，振幅大也不算 PCM 不連續
    const auto dop = frames24({0x05696900, static_cast<qint32>(0xFA696900u),
                               static_cast<qint32>(0xFA696900u), 0x05696900});
    check.feed(bytesOf(dop), 4);
    CHECK(check.dopBreaks() == 1);
    CHECK(check.pcmJumps() == 0);
}

TEST_CASE("AC-04-5：soak 經 DSD 包裝器播 DSD64，和 PCM 交替，沒有欠載與不連續") {
    const auto result = runSoak(auralis::app::kMinSoakSeconds, true,
                                std::make_shared<auralis::test::StatelessDopSink>());
    INFO(soakErrors(result).join(QStringLiteral("；")).toStdString());
    CHECK(soakErrors(result).isEmpty());
    CHECK(result.dsdSkipped.isEmpty());
    CHECK(result.dsdSink == QStringLiteral("DoP（測試）"));
    CHECK(result.dsdTracks >= 1);
    CHECK(result.formatChanges >= 1);
    CHECK(result.maxPcmStep > 0.01); // PCM 曲目真的有被檢查
    const QJsonObject dsd = soakJson(result).value(QStringLiteral("dsd")).toObject();
    CHECK(dsd.value(QStringLiteral("sink")).toString() == QStringLiteral("DoP（測試）"));
    CHECK(dsd.value(QStringLiteral("tracks")).toInt() == result.dsdTracks);
}

TEST_CASE("AC-04-5：要求 DSD 但沒有註冊包裝器時，DSD 部分略過並寫在結果，不算錯誤") {
    SoakResult result = cleanResult();
    result.dsdRequested = true;
    result.dsdSkipped = QStringLiteral("沒有註冊 DSD 包裝器");
    CHECK(soakErrors(result).isEmpty());
    const QJsonObject dsd = soakJson(result).value(QStringLiteral("dsd")).toObject();
    CHECK(dsd.value(QStringLiteral("skipped")).toString() == result.dsdSkipped);
    // 沒要求 DSD 時 JSON 沒有 dsd
    CHECK_FALSE(soakJson(cleanResult()).contains(QStringLiteral("dsd")));
}

TEST_CASE("AC-04-5：PCM 不連續與 DoP 標記斷掉都列進 errors") {
    SoakResult jumps = cleanResult();
    jumps.pcmJumps = 3;
    jumps.stepThreshold = 0.15;
    jumps.maxPcmStep = 0.5;
    REQUIRE(soakErrors(jumps).size() == 1);
    CHECK(soakErrors(jumps).first().contains(QStringLiteral("不連續 3 處")));

    SoakResult breaks = cleanResult();
    breaks.dopBreaks = 7;
    REQUIRE(soakErrors(breaks).size() == 1);
    CHECK(soakErrors(breaks).first().contains(QStringLiteral("7 框")));
}

TEST_CASE("長時間播放欠載：沒有欠載、同格式接續與換格式都有時沒有錯誤") {
    CHECK(soakErrors(cleanResult()).isEmpty());
}

TEST_CASE("長時間播放欠載：欠載大於 0 時列進 errors") {
    SoakResult result = cleanResult();
    result.underruns = 2;
    const QStringList errors = soakErrors(result);
    REQUIRE(errors.size() == 1);
    CHECK(errors.first().contains(QStringLiteral("欠載 2 次")));
}

TEST_CASE("長時間播放欠載：沒有換過格式或沒有同格式接續時列進 errors（檢查沒涵蓋到）") {
    SoakResult noSwitch = cleanResult();
    noSwitch.formatChanges = 0;
    CHECK(soakErrors(noSwitch).size() == 1);

    SoakResult noJoin = cleanResult();
    noJoin.sameFormatJoins = 0;
    CHECK(soakErrors(noJoin).size() == 1);
}

TEST_CASE("長時間播放欠載：播放失敗的原因列進 errors") {
    SoakResult result = cleanResult();
    result.error = QStringLiteral("開不了裝置");
    const QStringList errors = soakErrors(result);
    REQUIRE_FALSE(errors.isEmpty());
    CHECK(errors.first().contains(QStringLiteral("開不了裝置")));
}

TEST_CASE("長時間播放欠載：JSON 有秒數、曲目數、換格式次數與欠載次數") {
    SoakResult result = cleanResult();
    result.underruns = 4;
    const QJsonObject json = soakJson(result);
    CHECK(json.value(QStringLiteral("seconds")).toInt() == 5);
    CHECK(json.value(QStringLiteral("tracks")).toInt() == 3);
    CHECK(json.value(QStringLiteral("formatChanges")).toInt() == 1);
    CHECK(json.value(QStringLiteral("sameFormatJoins")).toInt() == 1);
    CHECK(json.value(QStringLiteral("underruns")).toInteger() == 4);
    CHECK(json.value(QStringLiteral("backend")).toString() == QStringLiteral("Capture"));
}

TEST_CASE("T-76：--soak --fullscreen 開登記的全螢幕頁，沒有全螢幕頁時回錯誤而不是略過") {
    auralis::core::UiRegistryImpl registry;
    auralis::core::NavigatorImpl navigator(registry);
    const QString missing = auralis::app::openSoakFullscreen(navigator);
    CHECK(missing.contains(QStringLiteral("沒有登記全螢幕頁")));
    CHECK_FALSE(navigator.fullscreen());

    registry.addFullscreenPage({QStringLiteral("fullscreen"), QStringLiteral("全螢幕"),
                                QUrl(QStringLiteral("qrc:/test/fullscreen.qml")), 0});
    CHECK(auralis::app::openSoakFullscreen(navigator).isEmpty());
    CHECK(navigator.fullscreen());
    CHECK(navigator.currentPage() == QStringLiteral("fullscreen"));
}

TEST_CASE("T-76：--soak --fullscreen 期間離開全螢幕（Esc、F11）列進 errors，只記一次") {
    auralis::core::UiRegistryImpl registry;
    auralis::core::NavigatorImpl navigator(registry);
    registry.addFullscreenPage({QStringLiteral("fullscreen"), QStringLiteral("全螢幕"),
                                QUrl(QStringLiteral("qrc:/test/fullscreen.qml")), 0});
    REQUIRE(auralis::app::openSoakFullscreen(navigator).isEmpty());
    FullscreenSoakWatch watch;
    auralis::app::watchSoakFullscreen(navigator, watch);
    CHECK(navigator.enterFullscreen()); // 已經在全螢幕：不算離開
    CHECK(watch.interruptions.isEmpty());

    REQUIRE(navigator.leaveFullscreen());
    REQUIRE(watch.interruptions.size() == 1);
    CHECK(watch.interruptions.first().contains(QStringLiteral("離開全螢幕")));
    REQUIRE(navigator.toggleFullscreen());
    REQUIRE(navigator.toggleFullscreen());
    CHECK(watch.interruptions.size() == 1);
    CHECK_FALSE(watch.stop.load());
}

TEST_CASE("T-76：--soak --fullscreen 的事件迴圈先結束時列進 errors，soak 不等滿秒數就停") {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_core_tests";
        static char* argv[] = {name, nullptr};
        new QCoreApplication(argc, argv); // 整個測試行程共用，不釋放
    }
    FullscreenSoakWatch watch;
    std::optional<SoakResult> result;
    std::unique_ptr<QThread> thread(
        QThread::create([&] { result = runSoak(30, false, {}, &watch.stop); }));
    const auto start = std::chrono::steady_clock::now();
    thread->start();
    // 模擬使用者在 soak 跑完前關掉主視窗：事件迴圈先返回
    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();
    auralis::app::finishSoakAfterUi(*thread, watch);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    REQUIRE(result.has_value());
    CHECK(result->seconds < 5);
    REQUIRE(watch.interruptions.size() == 1);
    CHECK(watch.interruptions.first().contains(QStringLiteral("跑完前結束")));

    // 正常路徑：soak 先跑完（事件迴圈因此結束）時不記錯誤
    FullscreenSoakWatch done;
    std::unique_ptr<QThread> finished(QThread::create([] {}));
    finished->start();
    REQUIRE(finished->wait());
    auralis::app::finishSoakAfterUi(*finished, done);
    CHECK(done.interruptions.isEmpty());
}
