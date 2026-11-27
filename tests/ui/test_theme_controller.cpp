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

#include <algorithm>
#include <cmath>

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
    CHECK(theme.fullscreenTheme() == QStringLiteral("dark")); // 預設＝v1.0 的行為

    QSignalSpy darkChanged(&theme, &ThemeController::darkChanged);
    QSignalSpy modeChanged(&theme, &ThemeController::modeChanged);
    theme.setFullscreen(true);
    CHECK(theme.fullscreen());
    CHECK(theme.dark());
    CHECK(darkChanged.count() == 1);
    // 使用者的設定不變：mode 照舊、settings.ini 照舊
    CHECK(theme.mode() == QStringLiteral("paper"));
    CHECK(modeChanged.isEmpty());
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/theme").toString() ==
          QStringLiteral("paper"));
    theme.setFullscreen(true); // 重複設定不再發訊號
    CHECK(darkChanged.count() == 1);

    // 離開：回到 Paper
    theme.setFullscreen(false);
    CHECK_FALSE(theme.dark());
    CHECK(darkChanged.count() == 2);

    // 全螢幕期間改成跟隨系統、系統變淺色：期間維持 Void，離開後套用新設定（淺色）
    theme.setMode(QStringLiteral("void"));
    theme.setFullscreen(true);
    theme.setMode(QStringLiteral("system"));
    hints->setColorScheme(Qt::ColorScheme::Light);
    CHECK(theme.dark());
    theme.setFullscreen(false);
    CHECK_FALSE(theme.dark());
    CHECK(QSettings(settings.path(), QSettings::IniFormat).value("ui/theme").toString() ==
          QStringLiteral("system"));
}

TEST_CASE("AC-17-6：全螢幕主題強制深色、強制淺色或跟隨主程式；不寫設定") {
    ColorSchemeGuard guard;
    SettingsFile settings;
    QStyleHints* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    ThemeController theme(settings.path(), hints, [] { return true; });
    theme.setMode(QStringLiteral("void"));

    // 強制淺色：主程式是 Void，全螢幕是 Paper；離開回到 Void
    theme.setFullscreenTheme(QStringLiteral("light"));
    CHECK(theme.dark()); // 不在全螢幕時不影響
    theme.setFullscreen(true);
    CHECK_FALSE(theme.dark());
    // 全螢幕期間改主程式主題不影響全螢幕
    theme.setMode(QStringLiteral("paper"));
    theme.setMode(QStringLiteral("void"));
    CHECK_FALSE(theme.dark());
    // 全螢幕中切換（T 鍵）：同一個呼叫裡就換好
    theme.setFullscreenTheme(QStringLiteral("dark"));
    CHECK(theme.dark());
    theme.setFullscreenTheme(QStringLiteral("light"));
    CHECK_FALSE(theme.dark());
    theme.setFullscreen(false);
    CHECK(theme.dark());

    // 跟隨主程式：主程式跟隨系統時也跟著系統變
    theme.setFullscreenTheme(QStringLiteral("follow"));
    theme.setMode(QStringLiteral("system"));
    theme.setFullscreen(true);
    CHECK(theme.dark());
    hints->setColorScheme(Qt::ColorScheme::Light);
    CHECK_FALSE(theme.dark());
    theme.setMode(QStringLiteral("void"));
    CHECK(theme.dark());
    theme.setFullscreen(false);

    // 不認得的值當成深色（預設）；全螢幕主題不寫進 settings.ini
    theme.setFullscreenTheme(QStringLiteral("亂寫"));
    CHECK(theme.fullscreenTheme() == QStringLiteral("dark"));
    CHECK_FALSE(QSettings(settings.path(), QSettings::IniFormat)
                    .allKeys()
                    .join(u',')
                    .contains(QStringLiteral("ullscreen")));
}

TEST_CASE("AC-17-6：全螢幕中換主題時顏色過渡關掉（instantThemeSwitch），進出全螢幕照舊") {
    ColorSchemeGuard guard;
    SettingsFile settings;
    ThemeController theme(settings.path(), nullptr, [] { return true; });
    theme.setMode(QStringLiteral("paper"));
    // 換主題的那一刻 instantThemeSwitch 是 true（Theme.motionState 用它歸零），之後回到 false
    QList<bool> duringChange;
    QObject::connect(&theme, &ThemeController::darkChanged,
                     [&] { duringChange.append(theme.instantThemeSwitch()); });
    QSignalSpy instantChanged(&theme, &ThemeController::instantThemeSwitchChanged);
    theme.setFullscreenTheme(QStringLiteral("follow"));
    theme.setFullscreen(true); // 跟隨主程式：進全螢幕不換主題
    theme.setMode(QStringLiteral("void"));
    theme.setFullscreenTheme(QStringLiteral("light"));
    theme.setFullscreen(false); // 離開全螢幕：主視窗照舊淡入（T-94）
    REQUIRE(duringChange.size() == 3);
    CHECK(duringChange[0]);
    CHECK(duringChange[1]);
    CHECK_FALSE(duringChange[2]);
    CHECK_FALSE(theme.instantThemeSwitch());
    CHECK(instantChanged.count() == 4);
}

namespace {

/// WCAG 2 的相對亮度與對比（DESIGN §2.3）。
double channel(quint32 rgb, int shift) {
    const double c = static_cast<double>((rgb >> shift) & 0xff) / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(quint32 rgb) {
    return 0.2126 * channel(rgb, 16) + 0.7152 * channel(rgb, 8) + 0.0722 * channel(rgb, 0);
}

double contrast(quint32 a, quint32 b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// HSL 的明度與飽和度（0–1）。
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

} // namespace

TEST_CASE("AC-17-6：淺色全螢幕底色＝封面主色提亮（明度 ≥ 0.92、飽和度 ≤ 0.30）；極端顏色") {
    // 沒有封面：Paper 的 bg
    CHECK(ThemeController::lightBackdropRgb(std::nullopt) == 0xf4f2eeu);
    // 純黑 → 灰（明度 0.92，飽和度 0）
    CHECK(ThemeController::lightBackdropRgb(0x000000) == 0xebebebu);
    // 純白：已經夠亮，照原樣
    CHECK(ThemeController::lightBackdropRgb(0xffffff) == 0xffffffu);
    // 高飽和紅：色相 0，飽和度 1 → 0.30、明度 0.5 → 0.92：R = 0.944、G = B = 0.896
    CHECK(ThemeController::lightBackdropRgb(0xff0000) == 0xf1e4e4u);

    // QML 的入口：顏色字串或數字；空白、undefined 是沒有封面
    SettingsFile settings;
    ThemeController theme(settings.path(), nullptr, [] { return true; });
    CHECK(theme.lightBackdrop(QVariant()) == QColor(0xf4f2ee));
    CHECK(theme.lightBackdrop(QString()) == QColor(0xf4f2ee));
    CHECK(theme.lightBackdrop(QStringLiteral("#ff0000")) == QColor(0xf1e4e4));
    CHECK(theme.lightBackdrop(QColor(0, 0, 0)) == QColor(0xebebeb));
}

TEST_CASE("AC-17-6：任何輸入色提亮後，Paper 的 ink、inkMuted、amber、signal 對比 ≥ 4.5:1") {
    // DESIGN §2.2 Paper 的值
    constexpr quint32 kInk = 0x0a0a0a;
    constexpr quint32 kInkMuted = 0x57534c;
    constexpr quint32 kAmber = 0x8f5810;
    constexpr quint32 kSignal = 0xb3261e;
    constexpr quint32 kLineStrong = 0x7a756c;
    QList<quint32> inputs{0xffffff, 0x000000, 0xff0000, 0x0000ff, 0x4040ff, 0x808080};
    for (quint32 r = 0; r < 256; r += 17) {
        for (quint32 g = 0; g < 256; g += 17) {
            for (quint32 b = 0; b < 256; b += 17) {
                inputs.append((r << 16) | (g << 8) | b);
            }
        }
    }
    double worstAmber = 100;
    double worstLine = 100;
    for (const quint32 input : inputs) {
        const quint32 out = ThemeController::lightBackdropRgb(input);
        INFO("輸入 " << QString::number(input, 16).toStdString() << " → "
                     << QString::number(out, 16).toStdString());
        CHECK(contrast(out, kInk) >= 4.5);
        CHECK(contrast(out, kInkMuted) >= 4.5);
        CHECK(contrast(out, kAmber) >= 4.5);
        CHECK(contrast(out, kSignal) >= 4.5);
        CHECK(contrast(out, kLineStrong) >= 3.0); // 進度條軌道（非文字）
        worstAmber = std::min(worstAmber, contrast(out, kAmber));
        worstLine = std::min(worstLine, contrast(out, kLineStrong));
        const auto [l, s] = lightnessSaturation(out);
        CHECK(l >= 0.92 - 0.5 / 255); // 量化到 8 位元的誤差
        CHECK(s <= 0.30 + 0.05);
    }
    // 數字寫進回報與 DESIGN §2.3
    WARN("最差對比：amber " << worstAmber << "，lineStrong " << worstLine);
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
