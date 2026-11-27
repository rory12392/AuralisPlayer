#include "settings/ui_scale.h"

#include <QGuiApplication>
#include <QSettings>

#include <cmath>

#include <qt_windows.h>
#include <shellscalingapi.h>

namespace auralis::core {

namespace {

const QString kScaleKey = QStringLiteral("ui/scale");
const QString kSystem = QStringLiteral("system");
const QString kAppliedKey = QStringLiteral("ui/appliedScale");

UiScaleStartup g_startup;

QRect toRect(const RECT& r) {
    return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
}

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }
    UINT dpiX = 96;
    UINT dpiY = 96;
    MonitorInfo result;
    result.geometry = toRect(info.rcMonitor);
    result.available = toRect(info.rcWork);
    result.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        result.systemScale = dpiX / 96.0;
    }
    reinterpret_cast<QList<MonitorInfo>*>(data)->append(result);
    return TRUE;
}

/// 建立 QGuiApplication 之前程式還沒宣告 DPI 感知，Windows 給的是換算過的座標；
/// 這段期間把執行緒暫時切到每個螢幕各自的 DPI，取到的才是實體像素。
class PerMonitorDpiScope {
public:
    PerMonitorDpiScope()
        : m_previous(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PerMonitorDpiScope() {
        if (m_previous != nullptr) {
            SetThreadDpiAwarenessContext(m_previous);
        }
    }
    PerMonitorDpiScope(const PerMonitorDpiScope&) = delete;
    PerMonitorDpiScope& operator=(const PerMonitorDpiScope&) = delete;

private:
    DPI_AWARENESS_CONTEXT m_previous;
};

} // namespace

QStringList uiScaleValues() {
    return {kSystem,
            QStringLiteral("1.0"),
            QStringLiteral("1.25"),
            QStringLiteral("1.5"),
            QStringLiteral("1.75"),
            QStringLiteral("2.0")};
}

QString savedUiScale(const QString& settingsPath) {
    const QString value =
        QSettings(settingsPath, QSettings::IniFormat).value(kScaleKey, kSystem).toString();
    return uiScaleValues().contains(value) ? value : kSystem;
}

bool saveUiScale(const QString& settingsPath, const QString& value) {
    if (!uiScaleValues().contains(value)) {
        return false;
    }
    QSettings(settingsPath, QSettings::IniFormat).setValue(kScaleKey, value);
    return true;
}

double uiScaleFactor(const QString& value) {
    return value != kSystem && uiScaleValues().contains(value) ? value.toDouble() : 0;
}

QSize minWindowPhysical(double scale) {
    return QSize(static_cast<int>(std::ceil(kMinWindowLogical.width() * scale - 1e-9)),
                 static_cast<int>(std::ceil(kMinWindowLogical.height() * scale - 1e-9)));
}

bool uiScaleFits(double scale, const QSize& available) {
    const QSize needed = minWindowPhysical(scale);
    return needed.width() <= available.width() && needed.height() <= available.height();
}

int startupMonitor(const QList<MonitorInfo>& monitors, std::optional<QPoint> savedPosition,
                   double lastScale) {
    if (savedPosition) {
        for (int i = 0; i < monitors.size(); ++i) {
            const MonitorInfo& m = monitors[i];
            const double scale = lastScale > 0 ? lastScale : m.systemScale;
            const QRect logical(m.geometry.topLeft(),
                                QSize(static_cast<int>(m.geometry.width() / scale),
                                      static_cast<int>(m.geometry.height() / scale)));
            if (logical.contains(*savedPosition)) {
                return i;
            }
        }
    }
    for (int i = 0; i < monitors.size(); ++i) {
        if (monitors[i].primary) {
            return i;
        }
    }
    return monitors.isEmpty() ? -1 : 0;
}

UiScaleStartup decideUiScale(const QString& setting, const std::optional<MonitorInfo>& monitor) {
    UiScaleStartup result;
    result.setting = uiScaleValues().contains(setting) ? setting : kSystem;
    const double scale = uiScaleFactor(result.setting);
    if (scale <= 0) {
        return result;
    }
    if (monitor && !uiScaleFits(scale, monitor->available.size())) {
        result.notice = QStringLiteral("介面縮放 %1 在這個螢幕放不下，這次改用跟隨系統。")
                            .arg(percentText(scale));
        return result;
    }
    result.applied = scale;
    return result;
}

ScaleEnvironment scaleEnvironment(double scale, int screenCount) {
    const bool doubled = scale <= 1.0;
    QByteArrayList parts;
    for (int i = 0; i < screenCount; ++i) {
        parts.append(QByteArray::number(doubled ? scale * 2 : scale));
    }
    return {parts.join(';'), doubled ? QByteArray("0.5") : QByteArray()};
}

QString percentText(double scale) {
    return QStringLiteral("%1%").arg(std::lround(scale * 100));
}

QList<MonitorInfo> currentMonitors() {
    const PerMonitorDpiScope scope;
    QList<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

std::optional<MonitorInfo> monitorOfWindow(quintptr hwnd) {
    const PerMonitorDpiScope scope;
    const HMONITOR monitor =
        hwnd != 0 ? MonitorFromWindow(reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY)
                  : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    QList<MonitorInfo> found;
    collectMonitor(monitor, nullptr, nullptr, reinterpret_cast<LPARAM>(&found));
    if (found.isEmpty()) {
        return std::nullopt;
    }
    return found.constFirst();
}

void saveAppliedUiScale(const QString& settingsPath, double applied) {
    QSettings(settingsPath, QSettings::IniFormat).setValue(kAppliedKey, applied);
}

void saveAppliedUiScaleOnClose(const QString& settingsPath, double applied, bool positionSaved) {
    // 記住的位置還是上一次寫的，比例要留著配它
    if (positionSaved) {
        saveAppliedUiScale(settingsPath, applied);
    }
}

double lastAppliedUiScale(const QString& settingsPath) {
    const double value =
        QSettings(settingsPath, QSettings::IniFormat).value(kAppliedKey, 0).toDouble();
    return value > 0 ? value : 0;
}

UiScaleStartup decideStartupUiScale(const QString& settingsPath,
                                    const QList<MonitorInfo>& monitors) {
    const QString setting = savedUiScale(settingsPath);
    // 主視窗會開在記住的位置所在的螢幕（ShellState 的 ui/windowX、windowY）
    const QSettings settings(settingsPath, QSettings::IniFormat);
    std::optional<QPoint> position;
    if (settings.contains(QStringLiteral("ui/windowX")) &&
        settings.contains(QStringLiteral("ui/windowY"))) {
        position = QPoint(settings.value(QStringLiteral("ui/windowX")).toInt(),
                          settings.value(QStringLiteral("ui/windowY")).toInt());
    }
    // 記住的位置是上次以上次實際套用的比例寫的，不是這次要套用的
    const int index = startupMonitor(monitors, position, lastAppliedUiScale(settingsPath));
    return decideUiScale(setting,
                         index >= 0 ? std::optional<MonitorInfo>(monitors[index]) : std::nullopt);
}

UiScaleStartup applyUiScaleBeforeApp(const QString& settingsPath) {
    // Qt 6 預設就是 PassThrough；明確設定，125%、175% 才不會被別處改成整數
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    if (settingsPath.isEmpty()) {
        g_startup = UiScaleStartup{};
        return g_startup;
    }
    const QList<MonitorInfo> monitors = currentMonitors();
    g_startup = decideStartupUiScale(settingsPath, monitors);
    if (g_startup.applied > 0) {
        // 每個螢幕都指定同一個比例：Qt 以它取代平台給的 DPI 比例（不是相乘）
        const ScaleEnvironment env =
            scaleEnvironment(g_startup.applied, static_cast<int>(monitors.size()));
        qputenv("QT_SCREEN_SCALE_FACTORS", env.screenFactors);
        if (!env.globalFactor.isEmpty()) {
            qputenv("QT_SCALE_FACTOR", env.globalFactor);
        }
    }
    return g_startup;
}

const UiScaleStartup& startupUiScale() {
    return g_startup;
}

} // namespace auralis::core
