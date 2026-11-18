// 主題與主視窗狀態：跟隨系統、手動切換並保留、減少動畫、字型退回順序、視窗狀態保存。
#include "shell/shell_state.h"
#include "theme/theme_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>

#include <windows.h>

using auralis::ui::ShellState;
using auralis::ui::ThemeController;

namespace {

QString testRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

struct SettingsFile {
    QTemporaryDir dir{testRoot() + QStringLiteral("/theme-XXXXXX")};
    QString path() const { return dir.filePath(QStringLiteral("settings.ini")); }
};

/// 每個測試結束時把應用程式層級的深淺色設定還原
struct ColorSchemeGuard {
    ~ColorSchemeGuard() { QGuiApplication::styleHints()->unsetColorScheme(); }
};

} // namespace

TEST_CASE("AC-19-1：跟隨系統時，系統深淺色改變後 1 秒內跟著變") {
    ColorSchemeGuard guard;
    SettingsFile settings;
    QStyleHints* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    ThemeController theme(settings.path(), hints, [] { return true; });
    // 建立時就決定好，引擎建立前讀到的值就是對的（不閃爍）
    CHECK(theme.mode() == QStringLiteral("system"));
    CHECK(theme.dark());

    QSignalSpy changed(&theme, &ThemeController::darkChanged);
    QElapsedTimer timer;
    timer.start();
    hints->setColorScheme(Qt::ColorScheme::Light);
    if (changed.isEmpty()) {
        REQUIRE(changed.wait(1000));
    }
    CHECK(timer.elapsed() < 1000);
    CHECK_FALSE(theme.dark());
}

TEST_CASE("AC-19-2：手動選淺色或深色，重新啟動後保留，不受系統影響") {
    ColorSchemeGuard guard;
    SettingsFile settings;
    QStyleHints* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    {
        ThemeController theme(settings.path(), hints, [] { return true; });
        theme.setMode(QStringLiteral("paper"));
        CHECK_FALSE(theme.dark());
    }
    ThemeController reopened(settings.path(), hints, [] { return true; });
    CHECK(reopened.mode() == QStringLiteral("paper"));
    CHECK_FALSE(reopened.dark());
    hints->setColorScheme(Qt::ColorScheme::Light);
    hints->setColorScheme(Qt::ColorScheme::Dark);
    CHECK_FALSE(reopened.dark());

    // 截圖用的暫時主題不寫進設定；不認得的值當成跟隨系統
    reopened.overrideMode(QStringLiteral("void"));
    CHECK(reopened.dark());
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/theme").toString() ==
          QStringLiteral("paper"));
    reopened.setMode(QStringLiteral("亂寫"));
    CHECK(reopened.mode() == QStringLiteral("system"));
}

TEST_CASE("T-94：全螢幕期間強制 Void，不寫設定；期間改的主題設定在離開後套用") {
    ColorSchemeGuard guard;
    SettingsFile settings;
    QStyleHints* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    ThemeController theme(settings.path(), hints, [] { return true; });
    theme.setMode(QStringLiteral("paper"));
    REQUIRE_FALSE(theme.dark());

    QSignalSpy darkChanged(&theme, &ThemeController::darkChanged);
    QSignalSpy modeChanged(&theme, &ThemeController::modeChanged);
    theme.setForceVoid(true);
    CHECK(theme.forceVoid());
    CHECK(theme.dark());
    CHECK(darkChanged.count() == 1);
    // 使用者的設定不變：mode 照舊、settings.ini 照舊
    CHECK(theme.mode() == QStringLiteral("paper"));
    CHECK(modeChanged.isEmpty());
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/theme").toString() ==
          QStringLiteral("paper"));
    theme.setForceVoid(true); // 重複設定不再發訊號
    CHECK(darkChanged.count() == 1);

    // 離開：回到 Paper
    theme.setForceVoid(false);
    CHECK_FALSE(theme.dark());
    CHECK(darkChanged.count() == 2);

    // 全螢幕期間改成跟隨系統、系統變淺色：期間維持 Void，離開後套用新設定（淺色）
    theme.setMode(QStringLiteral("void"));
    theme.setForceVoid(true);
    theme.setMode(QStringLiteral("system"));
    hints->setColorScheme(Qt::ColorScheme::Light);
    CHECK(theme.dark());
    theme.setForceVoid(false);
    CHECK_FALSE(theme.dark());
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/theme").toString() ==
          QStringLiteral("system"));
}

TEST_CASE("主題：減少動畫跟著 Windows「顯示動畫」設定，WM_SETTINGCHANGE 時重新讀") {
    SettingsFile settings;
    bool animationOn = true;
    ThemeController theme(settings.path(), nullptr, [&] { return animationOn; });
    CHECK_FALSE(theme.reduceMotion());

    QSignalSpy changed(&theme, &ThemeController::reduceMotionChanged);
    animationOn = false;
    MSG message{};
    message.message = WM_SETTINGCHANGE;
    message.wParam = SPI_SETCLIENTAREAANIMATION;
    CHECK_FALSE(theme.nativeEventFilter("windows_generic_MSG", &message, nullptr));
    CHECK(theme.reduceMotion());
    CHECK(changed.count() == 1);

    // 真的系統讀取函式至少能呼叫（不改系統設定，只讀）
    (void)ThemeController::systemAnimationEnabled();
}

TEST_CASE("主題：字型 token 保留退回順序的字族清單，字級與字重照指定") {
    SettingsFile settings;
    const QStringList loaded = auralis::ui::loadBundledFonts();
    CHECK(loaded.contains(QStringLiteral("Jost")));
    CHECK(loaded.contains(QStringLiteral("IBM Plex Mono")));

    ThemeController theme(settings.path(), nullptr, [] { return true; });
    const QFont label = theme.makeFont(false, 12, 500, 1.5, true);
    CHECK(label.families() ==
          QStringList{"Jost", "Microsoft JhengHei UI", "Yu Gothic UI", "Segoe UI"});
    CHECK(label.pixelSize() == 12);
    CHECK(label.weight() == QFont::Medium);
    CHECK(label.capitalization() == QFont::AllUppercase);
    CHECK(theme.makeFont(true, 15, 400).families().first() == QStringLiteral("IBM Plex Mono"));
}

TEST_CASE("主視窗：大小、位置與面板開合存進 settings.ini，截圖模式不存") {
    SettingsFile settings;
    {
        ShellState fresh(settings.path());
        CHECK(fresh.width() == ShellState::kDefaultWidth);
        CHECK(fresh.panelOpen()); // 預設寬度 ≥ 1200
        fresh.setProperty("windowWidth", 1000);
        fresh.setProperty("windowX", 40);
        fresh.setProperty("panelOpen", false);
        fresh.save();
    }
    {
        ShellState reopened(settings.path());
        CHECK(reopened.width() == 1000);
        CHECK(reopened.property("hasPosition").toBool());
        CHECK_FALSE(reopened.panelOpen());

        reopened.useFixedSize(1600, 900);
        CHECK(reopened.panelOpen());
        reopened.save();
    }
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/windowWidth").toInt() == 1000);

    SettingsFile narrow;
    QSettings(narrow.path(), QSettings::IniFormat).setValue("ui/windowWidth", 1100);
    CHECK_FALSE(ShellState(narrow.path()).panelOpen()); // 寬度 < 1200 時預設收合
}
