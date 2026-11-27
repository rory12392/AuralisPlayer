// 輸出面板與設定頁的 DSD 輸出、ReplayGain 選項保持同步（M-03 完整驗證的日誌
// qt.qml.binding.removal）：點選某一邊之後另一邊跟著變、程式改設定兩邊都跟著變，
// 而且選中項的綁定不會被控制項點選時的寫入蓋掉。載入模組真正的 QML 區塊，點選走 SegmentedControl
// 本身的 choose()（滑鼠與鍵盤都經過它）。
#include "dop_sink.h"
#include "dsd_mode_controller.h"
#include "fake_outputs.h"
#include "fake_playback_service.h"
#include "replay_gain.h"
#include "replay_gain_controller.h"
#include "test_qt_app.h"

#include "shell/output_controller.h"
#include "theme/theme_controller.h"

#include <auralis/audio_output.h>
#include <auralis/module_settings.h>

#include <catch2/catch_test_macros.hpp>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QVariantMap>
#include <QtPlugin>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)

using auralis::mod::out::DsdMode;
using auralis::mod::out::DsdModeController;
using auralis::mod::out::ReplayGainController;
using auralis::test::FakePlaybackService;

namespace {

/// QML 要 QGuiApplication；ctest 每個測試案例各自一個行程，所以這裡建立的就是這個行程唯一的。
void ensureGuiApp() {
    auralis::test::ensureQtApp<QGuiApplication>("auralis_out_tests");
    REQUIRE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr);
}

class MemorySettings final : public auralis::ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { values[key] = value; }
    bool contains(const QString& key) const override { return values.contains(key); }
    void remove(const QString& key) override { values.remove(key); }
    QVariantMap values;
};

class NoOutputs final : public auralis::OutputRegistry {
public:
    void add(std::shared_ptr<auralis::IOutputBackendFactory>) override {}
    void setDsdSink(std::shared_ptr<auralis::DsdSink>) override {}
};

/// 收集「綁定被覆寫」的日誌（qt.qml.binding.removal）。
QStringList g_removals;
QtMessageHandler g_previous = nullptr;

void collect(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (message.contains(QLatin1String("Overwriting binding"))) {
        g_removals.append(message);
    }
    if (g_previous != nullptr) {
        g_previous(type, context, message);
    }
}

/// 共用的 QML 環境：主題、輸出清單（DSD 區塊從它取目前的輸出路徑）。
struct QmlRig {
    QmlRig() {
        theme = std::make_unique<auralis::ui::ThemeController>(
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/section-sync.ini"), nullptr,
            [] { return true; });
        auralis::ui::ThemeController::setInstance(theme.get());
        output =
            std::make_unique<auralis::ui::OutputController>(playback, nullptr, fakeOutputs.hooks());
        auralis::ui::OutputController::setInstance(output.get());
        QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.binding.removal.info=true"));
        g_removals.clear();
        g_previous = qInstallMessageHandler(collect);
    }
    ~QmlRig() { qInstallMessageHandler(g_previous); }

    /// 載入模組的區塊，回傳裡面叫 name 的 SegmentedControl。
    QObject* load(const char* file, const char* name) {
        QQmlComponent component(
            &engine, QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/") + QLatin1String(file)));
        std::unique_ptr<QObject> object(component.create());
        INFO(component.errorString().toStdString());
        REQUIRE(object != nullptr);
        QObject* control = object->findChild<QObject*>(QLatin1String(name));
        REQUIRE(control != nullptr);
        sections.push_back(std::move(object));
        return control;
    }

    /// 第一個成員：QGuiApplication 要比下面的 QQmlEngine 先建立
    struct App {
        App() { ensureGuiApp(); }
    } app;
    FakePlaybackService playback;
    auralis::test::FakeOutputs fakeOutputs;
    std::unique_ptr<auralis::ui::ThemeController> theme;
    std::unique_ptr<auralis::ui::OutputController> output;
    QQmlEngine engine;
    std::vector<std::unique_ptr<QObject>> sections;
};

/// 使用者點了第 index 格（滑鼠與方向鍵都走 choose）。
void choose(QObject* control, int index) {
    REQUIRE(QMetaObject::invokeMethod(control, "choose", Q_ARG(QVariant, QVariant(index))));
}

int current(QObject* control) {
    return control->property("currentIndex").toInt();
}

} // namespace

TEST_CASE("AC-04-2：DSD 輸出在輸出面板與設定頁點選後兩邊同步，程式改設定時也跟著變") {
    QmlRig rig;
    MemorySettings settings;
    NoOutputs outputs;
    DsdModeController controller(settings, outputs, rig.playback, nullptr,
                                 [](DsdMode, quint32) -> std::shared_ptr<auralis::DsdSink> {
                                     return std::make_shared<auralis::mod::out::DopSink>();
                                 });
    DsdModeController::setInstance(&controller);
    QObject* panel = rig.load("DsdPanelSection.qml", "dsdPanelModes");
    QObject* page = rig.load("DsdSettingsSection.qml", "dsdSettingsModes");
    CHECK(current(panel) == 0);
    CHECK(current(page) == 0);

    // 輸出面板點「轉 PCM」→ 設定頁跟著變；再從設定頁點回 DoP → 輸出面板跟著變（每邊點兩次以上，
    // 原本第二次點選時就會蓋掉綁定）
    for (int round = 0; round < 2; ++round) {
        choose(panel, 2);
        CHECK(controller.modeIndex() == 2);
        CHECK(current(page) == 2);
        choose(page, 0);
        CHECK(controller.modeIndex() == 0);
        CHECK(current(panel) == 0);
    }
    // 程式改設定（例：換輸出路徑後實際方式改變）兩邊都跟著變
    controller.selectMode(2);
    CHECK(current(panel) == 2);
    CHECK(current(page) == 2);
    // 不能用的方式（原生 DSD 要 ASIO）點了不接受，選中項留在實際使用的方式
    choose(panel, 1);
    CHECK(current(panel) == 2);
    INFO(g_removals.join(QLatin1Char('\n')).toStdString());
    CHECK(g_removals.isEmpty());
    rig.sections.clear(); // 區塊先於控制器刪掉，綁定不會讀到已刪的單例
    DsdModeController::setInstance(nullptr);
}

TEST_CASE("AC-06-1：ReplayGain 在輸出面板點選後設定跟著變，設定頁（程式）改了輸出面板也跟著變") {
    QmlRig rig;
    MemorySettings settings;
    ReplayGainController controller(settings, rig.playback,
                                    std::make_shared<auralis::mod::out::RgSettings>(),
                                    std::make_shared<auralis::mod::out::RgAppliedLog>());
    ReplayGainController::setInstance(&controller);
    QObject* panel = rig.load("ReplayGainPanelSection.qml", "replayGainPanelModes");
    CHECK(current(panel) == 0);
    for (int round = 0; round < 2; ++round) {
        choose(panel, 2);
        CHECK(controller.modeIndex() == 2);
        // 設定頁的區塊用同一個單例：在那邊選了「音軌」
        controller.selectMode(1);
        CHECK(current(panel) == 1);
        choose(panel, 0);
        CHECK(controller.modeIndex() == 0);
    }
    INFO(g_removals.join(QLatin1Char('\n')).toStdString());
    CHECK(g_removals.isEmpty());
    rig.sections.clear();
    ReplayGainController::setInstance(nullptr);
}

TEST_CASE("AC-03-0：沒有 ASIO 外掛時輸出面板的「原生 DSD」停用並寫「需要 ASIO 外掛」，"
          "區塊帶到設定頁的外掛分區；有外掛時不顯示") {
    for (const bool nativeDsd : {false, true}) {
        QmlRig rig;
        MemorySettings settings;
        NoOutputs outputs;
        DsdModeController controller(
            settings, outputs, rig.playback, nullptr,
            [](DsdMode, quint32) -> std::shared_ptr<auralis::DsdSink> {
                return std::make_shared<auralis::mod::out::DopSink>();
            },
            nativeDsd);
        DsdModeController::setInstance(&controller);
        QObject* modes = rig.load("DsdPanelSection.qml", "dsdPanelModes");
        QObject* section = rig.sections.back().get();
        QObject* hint = section->findChild<QObject*>(QStringLiteral("dsdPanelPluginHint"));
        REQUIRE(hint != nullptr);
        REQUIRE(section->findChild<QObject*>(QStringLiteral("dsdPanelPluginLink")) != nullptr);
        const QVariantMap native = modes->property("model").toList().at(1).toMap();
        const QString reason = native.value(QStringLiteral("reason")).toString();
        if (nativeDsd) {
            CHECK_FALSE(hint->property("visible").toBool());
            // 有外掛時只差沒有選 ASIO（原因不是「需要 ASIO 外掛」）
            CHECK(reason != QStringLiteral("需要 ASIO 外掛"));
        } else {
            CHECK(hint->property("visible").toBool());
            CHECK_FALSE(native.value(QStringLiteral("enabled")).toBool());
            CHECK(reason == QStringLiteral("需要 ASIO 外掛"));
        }
        rig.sections.clear();
        DsdModeController::setInstance(nullptr);
    }
}
