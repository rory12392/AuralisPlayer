#include "self_check.h"

#include "audio/wasapi_backend.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace auralis::app {

namespace {

/// 專輯數這麼久沒變（分批載入完）才開始捲。
constexpr int kBenchSettleMs = 1500;
/// 等不到封面牆或專輯時放棄。
constexpr int kBenchTimeoutMs = 120000;
constexpr int kBenchPollMs = 200;
/// 每一幀捲動半列（約 1 萬 px／秒，快速捲動）。
constexpr double kBenchStepRows = 0.5;

QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root == nullptr || root->objectName() == name) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

double rounded(double value) {
    return std::round(value * 10.0) / 10.0;
}

struct AlbumWallBench {
    QQuickWindow* window = nullptr;
    std::function<void(QJsonObject)> done;
    QTimer* poll = nullptr;
    QMetaObject::Connection frames;
    QElapsedTimer waited;
    QElapsedTimer stableFor;
    QElapsedTimer clock;
    QQuickItem* grid = nullptr;
    int lastCount = -1;
    qint64 lastFrameNs = -1;
    std::vector<qint64> intervals;
    double step = 0;
    double minY = 0;
    double maxY = 0;
    bool down = true;

    double real(const char* name) const { return grid->property(name).toDouble(); }

    void finish(QJsonObject result) {
        QObject::disconnect(frames);
        if (poll != nullptr) {
            poll->stop();
            poll->deleteLater();
            poll = nullptr;
        }
        done(std::move(result));
    }

    QJsonObject stats() const {
        std::vector<double> fps;
        qint64 total = 0;
        qint64 longest = 0;
        for (const qint64 ns : intervals) {
            total += ns;
            longest = std::max(longest, ns);
            fps.push_back(1e9 / static_cast<double>(std::max<qint64>(ns, 1)));
        }
        std::sort(fps.begin(), fps.end());
        const auto frameCount = static_cast<double>(intervals.size());
        const double average = total > 0 ? frameCount * 1e9 / static_cast<double>(total) : 0;
        const double p5 = fps.empty() ? 0 : fps[static_cast<size_t>(0.05 * (frameCount - 1))];
        const double lowest = longest > 0 ? 1e9 / static_cast<double>(longest) : 0;
        return {{QStringLiteral("albums"), lastCount},
                {QStringLiteral("size"),
                 QStringLiteral("%1x%2").arg(window->width()).arg(window->height())},
                {QStringLiteral("stepPx"), rounded(step)},
                {QStringLiteral("frames"), static_cast<int>(intervals.size())},
                {QStringLiteral("seconds"), rounded(static_cast<double>(total) / 1e9)},
                {QStringLiteral("avgFps"), rounded(average)},
                {QStringLiteral("minFps"), rounded(lowest)},
                {QStringLiteral("p5Fps"), rounded(p5)},
                {QStringLiteral("thresholdFps"), kAlbumWallMinFps},
                {QStringLiteral("passed"), average >= kAlbumWallMinFps && p5 >= kAlbumWallMinFps}};
    }

    /// 每畫好一幀：記間隔、再推一步。往下到底後往上，回到頂就結束。
    void onFrame(const std::shared_ptr<AlbumWallBench>& self) {
        const qint64 now = clock.nsecsElapsed();
        if (lastFrameNs >= 0) {
            intervals.push_back(now - lastFrameNs);
        }
        lastFrameNs = now;
        double y = real("contentY");
        if (down) {
            y = std::min(y + step, maxY);
            down = y < maxY;
        } else {
            y = std::max(y - step, minY);
            if (y <= minY) {
                grid->setProperty("contentY", y);
                self->finish(stats());
                return;
            }
        }
        grid->setProperty("contentY", y);
    }

    /// 等封面牆出現、專輯數穩定；好了就開始捲。
    void onPoll(const std::shared_ptr<AlbumWallBench>& self) {
        if (waited.elapsed() > kBenchTimeoutMs) {
            finish(
                {{QStringLiteral("error"),
                  grid == nullptr ? QStringLiteral("找不到封面牆（albumGrid），LIB 模組沒有啟用？")
                                  : QStringLiteral("專輯數一直是 0 或一直在變")}});
            return;
        }
        grid = findItem(window->contentItem(), QStringLiteral("albumGrid"));
        if (grid == nullptr || !grid->isVisible()) {
            return;
        }
        const int count = grid->property("count").toInt();
        if (count != lastCount || count == 0) {
            lastCount = count;
            stableFor.restart();
            return;
        }
        if (stableFor.elapsed() < kBenchSettleMs) {
            return;
        }
        poll->stop();
        minY = real("originY") - real("topMargin");
        maxY = real("originY") + real("contentHeight") + real("bottomMargin") - real("height");
        step = real("cellHeight") * kBenchStepRows;
        if (maxY <= minY || step <= 0) {
            finish({{QStringLiteral("error"), QStringLiteral("專輯太少，封面牆捲不動")},
                    {QStringLiteral("albums"), count}});
            return;
        }
        grid->setProperty("contentY", minY);
        clock.start();
        frames = QObject::connect(window, &QQuickWindow::frameSwapped, window,
                                  [self] { self->onFrame(self); });
        window->update();
    }
};

} // namespace

void startAlbumWallBench(QQuickWindow* window, std::function<void(QJsonObject)> done) {
    auto bench = std::make_shared<AlbumWallBench>();
    bench->window = window;
    bench->done = std::move(done);
    bench->poll = new QTimer(window);
    bench->waited.start();
    bench->stableFor.start();
    QObject::connect(bench->poll, &QTimer::timeout, window, [bench] { bench->onPoll(bench); });
    bench->poll->start(kBenchPollMs);
}

QStringList albumWallErrors(const QJsonObject& result) {
    if (result.contains(QStringLiteral("error"))) {
        return {QStringLiteral("封面牆量測失敗：%1")
                    .arg(result.value(QStringLiteral("error")).toString())};
    }
    if (!result.value(QStringLiteral("passed")).toBool()) {
        return {QStringLiteral("封面牆捲動低於 %1 fps：平均 %2、第 5 百分位 %3")
                    .arg(kAlbumWallMinFps)
                    .arg(result.value(QStringLiteral("avgFps")).toDouble())
                    .arg(result.value(QStringLiteral("p5Fps")).toDouble())};
    }
    return {};
}

QString albumWallSummary(const QJsonObject& result) {
    if (result.contains(QStringLiteral("error"))) {
        return albumWallErrors(result).constFirst();
    }
    return QStringLiteral("封面牆捲動（%1 張專輯、%2、%3 幀）：平均 %4 fps、最低 %5 fps、"
                          "第 5 百分位 %6 fps，門檻 %7 fps：%8")
        .arg(result.value(QStringLiteral("albums")).toInt())
        .arg(result.value(QStringLiteral("size")).toString())
        .arg(result.value(QStringLiteral("frames")).toInt())
        .arg(result.value(QStringLiteral("avgFps")).toDouble())
        .arg(result.value(QStringLiteral("minFps")).toDouble())
        .arg(result.value(QStringLiteral("p5Fps")).toDouble())
        .arg(kAlbumWallMinFps)
        .arg(result.value(QStringLiteral("passed")).toBool() ? QStringLiteral("通過")
                                                             : QStringLiteral("未過"));
}

QJsonObject ffmpegBuildInfo() {
    const QString configuration = QString::fromUtf8(avcodec_configuration());
    const QStringList flags = configuration.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return {
        {QStringLiteral("version"), QString::fromUtf8(av_version_info())},
        {QStringLiteral("configuration"), configuration},
        {QStringLiteral("license"), QString::fromUtf8(avcodec_license())},
        {QStringLiteral("gpl"), flags.contains(QStringLiteral("--enable-gpl"))},
        {QStringLiteral("nonfree"), flags.contains(QStringLiteral("--enable-nonfree"))},
        {QStringLiteral("networkDisabled"), flags.contains(QStringLiteral("--disable-network"))}};
}

void collectModuleErrors(SelfCheckReport& report) {
    for (const auto& module : report.modules) {
        if (module.state == core::ModuleStatus::State::Failed) {
            report.errors << QStringLiteral("模組 %1 啟動失敗：%2").arg(module.code, module.error);
        }
    }
}

QByteArray toJson(const SelfCheckReport& report) {
    QJsonArray modules;
    for (const auto& module : report.modules) {
        QJsonObject item{{QStringLiteral("code"), module.code},
                         {QStringLiteral("name"), module.name},
                         {QStringLiteral("version"), module.version},
                         {QStringLiteral("state"), core::toString(module.state)}};
        if (!module.error.isEmpty()) {
            item.insert(QStringLiteral("error"), module.error);
        }
        modules.append(item);
    }

    QJsonArray plugins;
    for (const auto& plugin : report.plugins) {
        QJsonObject item{{QStringLiteral("id"), plugin.id},
                         {QStringLiteral("name"), plugin.name},
                         {QStringLiteral("type"), plugin.type},
                         {QStringLiteral("version"), plugin.version},
                         {QStringLiteral("state"), core::toString(plugin.state)},
                         {QStringLiteral("pending"), core::toString(plugin.pending)},
                         {QStringLiteral("builtin"), plugin.builtin}};
        if (!plugin.reason.isEmpty()) {
            item.insert(QStringLiteral("reason"), plugin.reason);
        }
        plugins.append(item);
    }

    QJsonObject root{{QStringLiteral("app"), QStringLiteral("AuralisPlayer")},
                     {QStringLiteral("version"), report.version},
                     {QStringLiteral("devBuild"), report.devBuild},
                     {QStringLiteral("dataDir"), report.dataDir},
                     {QStringLiteral("schemaVersion"), report.schemaVersion},
                     {QStringLiteral("ffmpeg"), ffmpegBuildInfo()},
                     {QStringLiteral("modules"), modules},
                     {QStringLiteral("plugins"), plugins},
                     {QStringLiteral("errors"), QJsonArray::fromStringList(report.errors)}};
    if (report.outputs) {
        root.insert(QStringLiteral("outputs"), *report.outputs);
    }
    if (report.soak) {
        root.insert(QStringLiteral("soak"), *report.soak);
    }
    if (report.albumWall) {
        root.insert(QStringLiteral("albumWall"), *report.albumWall);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QJsonArray probeOutputs(const core::OutputRegistryImpl& registry) {
    QJsonArray paths;
    for (const auto& factory : registry.factories()) {
        auto wasapi = std::dynamic_pointer_cast<core::WasapiBackendFactory>(factory);
        QJsonArray devices;
        for (const auto& device : factory->devices()) {
            QJsonObject item{{QStringLiteral("id"), device.id},
                             {QStringLiteral("name"), device.name}};
            if (wasapi && wasapi->name() == QStringLiteral("WASAPI 獨占")) {
                auto formats = wasapi->probeExclusiveFormats(device.id);
                if (formats) {
                    item.insert(QStringLiteral("exclusiveFormats"),
                                QJsonArray::fromStringList(formats.value()));
                } else {
                    item.insert(QStringLiteral("error"), formats.error().message);
                }
            }
            devices.append(item);
        }
        paths.append(QJsonObject{{QStringLiteral("path"), factory->name()},
                                 {QStringLiteral("devices"), devices}});
    }
    return paths;
}

} // namespace auralis::app
