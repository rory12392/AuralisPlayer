// 曲目右鍵選單的「計算 ReplayGain」（T-88，AC-06-3、畫面規格 S-07）：載入模組真正的區塊
// R128TrackMenuSection.qml，確認收到 payloadJson 後觸發就交給單例 R128 的 computeTracks、
// 計算中停用、只帶查詢條件（超過 5,000 首）時停用。
#include "r128_controller.h"

#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "theme/theme_controller.h"

#include <auralis/job_runner.h>
#include <auralis/library_query.h>
#include <auralis/track_drag_payload.h>

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QVariantMap>

#include <memory>

using auralis::Error;
using auralis::JobRunner;
using auralis::Result;
using auralis::TrackInfo;
using auralis::TrackRef;
using auralis::mod::out::R128Controller;

namespace {

/// QML 要 QGuiApplication；ctest 每個測試案例各自一個行程，所以這裡建立的就是這個行程唯一的。
void ensureGuiApp() {
    static int argc = 1;
    static char name[] = "auralis_out_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QGuiApplication(argc, argv);
    }
    REQUIRE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr);
}

/// 空的音樂庫：計畫找不到任何曲目，工作很快結束（這裡只看選單項目有沒有把內容交出去）。
class EmptyLibrary final : public auralis::LibraryQuery {
public:
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const auralis::TrackFilter&) const override {
        return QList<TrackInfo>{};
    }
    Result<QList<auralis::AlbumInfo>> albums() const override {
        return QList<auralis::AlbumInfo>{};
    }
    Result<QStringList> artists() const override { return QStringList{}; }
    Result<QList<auralis::FolderInfo>> folders(qint64) const override {
        return QList<auralis::FolderInfo>{};
    }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
};

/// 主題、資料庫、R128 控制器與 QML 引擎。
struct Rig {
    Rig() {
        theme = std::make_unique<auralis::ui::ThemeController>(
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/r128-track-menu.ini"), nullptr,
            [] { return true; });
        auralis::ui::ThemeController::setInstance(theme.get());
        REQUIRE(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("library.db"));
        REQUIRE(auralis::core::migrateDatabase(path).hasValue());
        db = std::make_unique<auralis::core::ModuleDatabaseImpl>(QStringLiteral("OUT"), path);
        controller = std::make_unique<R128Controller>(
            jobs, library, *db,
            [](const TrackRef&) -> Result<std::unique_ptr<auralis::IAudioSource>> {
                return Error{QStringLiteral("測試不開檔"), 0, QStringLiteral("test")};
            },
            nullptr);
        R128Controller::setInstance(controller.get());
    }
    ~Rig() {
        if (controller) {
            controller->shutdown();
        }
    }

    /// 像 TrackMenu 一樣建立區塊：給 payloadJson（required property）。
    std::unique_ptr<QObject> load(const QString& payloadJson) {
        QQmlComponent component(
            &engine, QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/R128TrackMenuSection.qml")));
        std::unique_ptr<QObject> item(
            component.createWithInitialProperties({{QStringLiteral("payloadJson"), payloadJson}}));
        INFO(component.errorString().toStdString());
        REQUIRE(item != nullptr);
        return item;
    }

    bool waitIdle(int timeoutMs = 30000) {
        QElapsedTimer timer;
        timer.start();
        while (controller->running() && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return !controller->running();
    }

    /// 第一個成員：QGuiApplication 要比下面的 QQmlEngine 先建立
    struct App {
        App() { ensureGuiApp(); }
    } app;
    std::unique_ptr<auralis::ui::ThemeController> theme;
    QTemporaryDir dir{QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/r128-menu-XXXXXX")};
    std::unique_ptr<auralis::core::ModuleDatabaseImpl> db;
    EmptyLibrary library;
    JobRunner jobs;
    std::unique_ptr<R128Controller> controller;
    QQmlEngine engine;
};

QString payloadOf(int count) {
    auralis::TrackDragPayload payload;
    for (int i = 0; i < count; ++i) {
        payload.tracks.append({QStringLiteral("D:\\音樂\\A\\%1.flac").arg(i), 0});
    }
    return QString::fromUtf8(payload.toJson());
}

} // namespace

TEST_CASE("AC-06-3（T-88）：右鍵選單的「計算 ReplayGain」把收到的 payloadJson 交給 "
          "R128.computeTracks，計算中停用") {
    Rig rig;
    auto item = rig.load(payloadOf(2));
    CHECK(item->property("text").toString() == QStringLiteral("計算 ReplayGain"));
    CHECK(item->property("enabled").toBool());

    REQUIRE(QMetaObject::invokeMethod(item.get(), "triggered"));
    // computeTracks 收到內容就開始背景工作（已在算時不理會，所以選單項目跟著停用）
    CHECK(rig.controller->running());
    CHECK(rig.controller->status() == QStringLiteral("正在計算 ReplayGain…"));
    CHECK_FALSE(item->property("enabled").toBool());

    REQUIRE(rig.waitIdle());
    CHECK(rig.controller->status() != QStringLiteral("正在計算 ReplayGain…"));
    CHECK(item->property("enabled").toBool());
}

TEST_CASE("AC-06-3（T-88）：只帶查詢條件（超過 5,000 首）時「計算 ReplayGain」停用") {
    Rig rig;
    auralis::TrackDragPayload payload;
    payload.query = QStringLiteral("search=a");
    auto item = rig.load(QString::fromUtf8(payload.toJson()));
    CHECK(item->property("text").toString() == QStringLiteral("計算 ReplayGain"));
    CHECK_FALSE(item->property("enabled").toBool());
}
