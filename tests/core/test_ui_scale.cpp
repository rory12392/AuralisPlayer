// 介面縮放（AC-19-4）：ui/scale 的讀寫與預設、比例換算、放不下的判斷（最小視窗 960×640 換成實體像素
// 和螢幕可用範圍比）、啟動時用哪個螢幕、QT_SCREEN_SCALE_FACTORS 的值、設定頁控制器。
// 螢幕是假的（純函式與注入的螢幕來源），不讀也不改 Windows 的顯示設定。
#include "settings/ui_scale.h"
#include "settings/ui_scale_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantMap>

using auralis::core::MonitorInfo;

namespace {

MonitorInfo monitor(QRect geometry, QSize available, double systemScale, bool primary = false) {
    MonitorInfo m;
    m.geometry = geometry;
    m.available = QRect(geometry.topLeft(), available);
    m.systemScale = systemScale;
    m.primary = primary;
    return m;
}

} // namespace

TEST_CASE("AC-19-4：ui/scale 預設是跟隨系統，寫入後重新讀到同一個值，不合法的值不寫") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    CHECK(auralis::core::savedUiScale(path) == QStringLiteral("system"));
    for (const QString& value : auralis::core::uiScaleValues()) {
        REQUIRE(auralis::core::saveUiScale(path, value));
        CHECK(auralis::core::savedUiScale(path) == value);
    }
    CHECK_FALSE(auralis::core::saveUiScale(path, QStringLiteral("3.0")));
    CHECK(auralis::core::savedUiScale(path) == QStringLiteral("2.0"));
    // 手改成不合法的值時當作跟隨系統
    QSettings(path, QSettings::IniFormat)
        .setValue(QStringLiteral("ui/scale"), QStringLiteral("abc"));
    CHECK(auralis::core::savedUiScale(path) == QStringLiteral("system"));
}

TEST_CASE("AC-19-4：選項依序是跟隨系統、100%–200%，比例換算") {
    CHECK(auralis::core::uiScaleValues() ==
          QStringList{QStringLiteral("system"), QStringLiteral("1.0"), QStringLiteral("1.25"),
                      QStringLiteral("1.5"), QStringLiteral("1.75"), QStringLiteral("2.0")});
    CHECK(auralis::core::uiScaleFactor(QStringLiteral("system")) == 0);
    CHECK(auralis::core::uiScaleFactor(QStringLiteral("1.75")) == 1.75);
    CHECK(auralis::core::uiScaleFactor(QStringLiteral("0.5")) == 0);
    CHECK(auralis::core::percentText(1.25) == QStringLiteral("125%"));
    CHECK(auralis::core::percentText(1.75) == QStringLiteral("175%"));
}

TEST_CASE("AC-19-4：最小視窗的實體像素與放不下的判斷") {
    using auralis::core::minWindowPhysical;
    using auralis::core::uiScaleFits;
    CHECK(minWindowPhysical(1.0) == QSize(960, 640));
    CHECK(minWindowPhysical(1.25) == QSize(1200, 800));
    CHECK(minWindowPhysical(1.75) == QSize(1680, 1120));
    CHECK(minWindowPhysical(2.0) == QSize(1920, 1280));
    // 畫面規格的例子：1920×1032 可用時 150% 放得下、175% 放不下（高度不夠）
    CHECK(uiScaleFits(1.5, QSize(1920, 1032)));
    CHECK_FALSE(uiScaleFits(1.75, QSize(1920, 1032)));
    // 剛好等於可用範圍算放得下；寬或高差 1 都不行
    CHECK(uiScaleFits(2.0, QSize(1920, 1280)));
    CHECK_FALSE(uiScaleFits(2.0, QSize(1919, 1280)));
    CHECK_FALSE(uiScaleFits(2.0, QSize(1920, 1279)));
    // 4K（工作列 72 px）：200% 也放得下
    CHECK(uiScaleFits(2.0, QSize(3840, 2088)));
}

TEST_CASE("AC-19-4：啟動時跟隨系統不設比例；放得下照設定；放不下這次改跟隨系統、不改設定值") {
    const auto fourK = monitor(QRect(0, 0, 3840, 2160), QSize(3840, 2088), 1.5, true);
    const auto fhd = monitor(QRect(0, 0, 1920, 1080), QSize(1920, 1032), 1.25, true);

    const auto system = auralis::core::decideUiScale(QStringLiteral("system"), fourK);
    CHECK(system.applied == 0);
    CHECK(system.notice.isEmpty());

    const auto big = auralis::core::decideUiScale(QStringLiteral("2.0"), fourK);
    CHECK(big.applied == 2.0);
    CHECK(big.setting == QStringLiteral("2.0"));
    CHECK(big.notice.isEmpty());

    const auto misfit = auralis::core::decideUiScale(QStringLiteral("1.75"), fhd);
    CHECK(misfit.applied == 0);
    CHECK(misfit.setting == QStringLiteral("1.75"));
    CHECK(misfit.notice == QStringLiteral("介面縮放 175% 在這個螢幕放不下，這次改用跟隨系統。"));

    // 取不到螢幕時照設定套用
    CHECK(auralis::core::decideUiScale(QStringLiteral("1.25"), std::nullopt).applied == 1.25);
}

TEST_CASE("AC-19-4：多螢幕時每個螢幕都指定同一個比例（取代各自的 Windows 縮放）") {
    using auralis::core::scaleEnvironment;
    CHECK(scaleEnvironment(1.5, 1).screenFactors == QByteArray("1.5"));
    CHECK(scaleEnvironment(1.25, 3).screenFactors == QByteArray("1.25;1.25;1.25"));
    CHECK(scaleEnvironment(2.0, 2).screenFactors == QByteArray("2;2"));
    CHECK(scaleEnvironment(2.0, 2).globalFactor.isEmpty());
    // 100%：Qt 不採用 1 的螢幕比例，寫成 2 再乘 0.5
    CHECK(scaleEnvironment(1.0, 2).screenFactors == QByteArray("2;2"));
    CHECK(scaleEnvironment(1.0, 2).globalFactor == QByteArray("0.5"));
}

TEST_CASE("AC-19-4：啟動時的螢幕是記住的視窗位置所在的那個，沒有時是主螢幕") {
    // 左邊 4K（150%，主螢幕）、右邊 1080p（100%）
    const QList<MonitorInfo> monitors{
        monitor(QRect(0, 0, 3840, 2160), QSize(3840, 2088), 1.5, true),
        monitor(QRect(3840, 0, 1920, 1080), QSize(1920, 1040), 1.0)};
    CHECK(auralis::core::startupMonitor(monitors, std::nullopt, 0) == 0);
    CHECK(auralis::core::startupMonitor(monitors, QPoint(4000, 100), 0) == 1);
    CHECK(auralis::core::startupMonitor(monitors, QPoint(100, 100), 0) == 0);
    // 上次 200%：4K 的邏輯範圍只有 1920×1080，(2000, 100) 不在任何螢幕 → 主螢幕
    CHECK(auralis::core::startupMonitor(monitors, QPoint(2000, 100), 2.0) == 0);
    CHECK(auralis::core::startupMonitor({}, std::nullopt, 0) == -1);
}

TEST_CASE("AC-19-4：啟動時用上次實際套用的比例認出記住的螢幕，再用新設定判斷放不放得下") {
    // 主螢幕 1920×1080（100%）、右側 3840×2160（100%，原點 1920,0）
    const QList<MonitorInfo> monitors{
        monitor(QRect(0, 0, 1920, 1080), QSize(1920, 1040), 1.0, true),
        monitor(QRect(1920, 0, 3840, 2160), QSize(3840, 2120), 1.0)};
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    const auto remember = [&path](const QVariant& applied) {
        QSettings settings(path, QSettings::IniFormat);
        settings.clear();
        settings.setValue(QStringLiteral("ui/scale"), QStringLiteral("2.0"));
        settings.setValue(QStringLiteral("ui/windowX"), 4000);
        settings.setValue(QStringLiteral("ui/windowY"), 100);
        if (applied.isValid()) {
            settings.setValue(QStringLiteral("ui/appliedScale"), applied);
        }
    };
    using auralis::core::decideStartupUiScale;

    // 上次 100% 時視窗在右側 (4000,100)，改選 200% 重啟：右側放得下 200%
    remember(1.0);
    auto startup = decideStartupUiScale(path, monitors);
    CHECK(startup.applied == 2.0);
    CHECK(startup.notice.isEmpty());

    // 上次放不下、退回跟隨系統（記成 0）：用右側自己的系統縮放認螢幕
    remember(0.0);
    startup = decideStartupUiScale(path, monitors);
    CHECK(startup.applied == 2.0);

    // 沒有記錄（之前的版本都是跟隨系統）：當成跟隨系統
    remember(QVariant());
    startup = decideStartupUiScale(path, monitors);
    CHECK(startup.applied == 2.0);

    // 真的在主螢幕時照樣判斷放不下（主螢幕高 1040 < 1280）
    QSettings(path, QSettings::IniFormat).setValue(QStringLiteral("ui/windowX"), 100);
    startup = decideStartupUiScale(path, monitors);
    CHECK(startup.applied == 0);
    CHECK_FALSE(startup.notice.isEmpty());
}

TEST_CASE("AC-19-4：實際套用的比例寫入後讀回同一個值，沒有記錄時是跟隨系統（0）") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    CHECK(auralis::core::lastAppliedUiScale(path) == 0);
    auralis::core::saveAppliedUiScale(path, 1.75);
    CHECK(auralis::core::lastAppliedUiScale(path) == 1.75);
    auralis::core::saveAppliedUiScale(path, 0);
    CHECK(auralis::core::lastAppliedUiScale(path) == 0);
}

TEST_CASE("AC-19-4：最大化關閉不記比例，改比例後重啟仍認得出記住的螢幕") {
    // 主螢幕 1920×1080（100%，放不下 200%）、右側 3840×2160（100%，原點 1920,0，放得下 200%）
    const QList<MonitorInfo> monitors{
        monitor(QRect(0, 0, 1920, 1080), QSize(1920, 1040), 1.0, true),
        monitor(QRect(1920, 0, 3840, 2160), QSize(3840, 2120), 1.0)};
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    using auralis::core::decideStartupUiScale;
    using auralis::core::saveAppliedUiScaleOnClose;

    // 100% 時在右側的一般視窗關閉：位置（100% 的邏輯座標）和比例一起記下
    REQUIRE(auralis::core::saveUiScale(path, QStringLiteral("1.0")));
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/windowX"), 4500);
        settings.setValue(QStringLiteral("ui/windowY"), 100);
    }
    saveAppliedUiScaleOnClose(path, 1.0, true);

    // 改成 200% 重啟：認得出右側、放得下
    REQUIRE(auralis::core::saveUiScale(path, QStringLiteral("2.0")));
    auto startup = decideStartupUiScale(path, monitors);
    REQUIRE(startup.applied == 2.0);

    // 最大化後關閉：位置不寫（ShellState 只在一般視窗記位置），比例也不能寫
    saveAppliedUiScaleOnClose(path, startup.applied, false);

    // 再重啟：位置仍是 100% 的座標，要用 100% 解讀才認得出右側
    // （用 200% 解讀會落到主螢幕、放不下）
    CHECK(auralis::core::lastAppliedUiScale(path) == 1.0);
    CHECK(auralis::core::startupMonitor(monitors, QPoint(4500, 100),
                                        auralis::core::lastAppliedUiScale(path)) == 1);
    startup = decideStartupUiScale(path, monitors);
    CHECK(startup.applied == 2.0);
    CHECK(startup.notice.isEmpty());

    // 之後在 200% 的一般視窗關閉：位置與比例一起換成 200% 的
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/windowX"), 2500);
    }
    saveAppliedUiScaleOnClose(path, 2.0, true);
    CHECK(auralis::core::lastAppliedUiScale(path) == 2.0);
    CHECK(decideStartupUiScale(path, monitors).applied == 2.0);
}

TEST_CASE("AC-19-4：設定頁控制器：選項依螢幕停用、寫入設定、換螢幕重新判斷") {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    std::optional<MonitorInfo> current =
        monitor(QRect(0, 0, 1920, 1080), QSize(1920, 1032), 1.25, true);
    auralis::core::UiScaleController controller(path, QStringLiteral("system"),
                                                [&current] { return current; });
    CHECK(controller.value() == QStringLiteral("system"));
    CHECK(controller.systemScale() == 1.25);
    CHECK(controller.available() == QSize(1920, 1032));

    const auto fitsOf = [&controller] {
        QList<bool> fits;
        for (const QVariant& option : controller.options()) {
            fits.append(option.toMap().value(QStringLiteral("fits")).toBool());
        }
        return fits;
    };
    CHECK(fitsOf() == QList<bool>{true, true, true, true, false, false});
    const QVariantMap at175 = controller.options().at(4).toMap();
    CHECK(at175.value(QStringLiteral("width")).toInt() == 1680);
    CHECK(at175.value(QStringLiteral("height")).toInt() == 1120);

    QSignalSpy valueChanged(&controller, &auralis::core::UiScaleController::valueChanged);
    controller.setValue(QStringLiteral("1.5"));
    CHECK(valueChanged.count() == 1);
    CHECK(auralis::core::savedUiScale(path) == QStringLiteral("1.5"));
    controller.setValue(QStringLiteral("9"));
    CHECK(controller.value() == QStringLiteral("1.5"));

    // 換到 4K：全部放得下
    QSignalSpy screenChanged(&controller, &auralis::core::UiScaleController::screenChanged);
    current = monitor(QRect(0, 0, 3840, 2160), QSize(3840, 2088), 1.5, true);
    controller.refresh();
    CHECK(screenChanged.count() == 1);
    CHECK(fitsOf() == QList<bool>{true, true, true, true, true, true});

    // 取不到螢幕時不停用
    current.reset();
    controller.refresh();
    CHECK(fitsOf() == QList<bool>{true, true, true, true, true, true});
}
