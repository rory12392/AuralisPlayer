#pragma once

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace auralis::core {

/// 介面縮放（AC-19-4）：settings.ini 的 ui/scale，值是 system（跟隨系統，預設）或 1.0、1.25、
/// 1.5、1.75、2.0。數字是整個程式的絕對比例，取代 Windows 每個螢幕的縮放（不是乘在上面），
/// 多螢幕時每個螢幕都用同一個比例；在建立 QGuiApplication 之前決定，執行中不變。

/// 設定值，依選單順序：system、1.0、1.25、1.5、1.75、2.0。
QStringList uiScaleValues();

/// 記住的設定值；沒有記過或值不合法時是 system。
QString savedUiScale(const QString& settingsPath);

/// 寫進 ui/scale（不合法的值不寫，回傳 false）。
bool saveUiScale(const QString& settingsPath, const QString& value);

/// 設定值的比例：1.5 → 1.5；system 或不合法時是 0（跟隨系統）。
double uiScaleFactor(const QString& value);

/// 最小視窗（邏輯像素，介面規範 §4）。
inline constexpr QSize kMinWindowLogical{960, 640};

/// 比例 scale 時最小視窗佔的實體像素（無條件進位）。
QSize minWindowPhysical(double scale);

/// 最小視窗換成實體像素後放得進 available（實體像素，扣掉工作列的可用範圍）。
bool uiScaleFits(double scale, const QSize& available);

/// 一個螢幕：位置與可用範圍都是實體像素（虛擬桌面座標），systemScale 是 Windows
/// 的縮放（1.5＝150%）。
struct MonitorInfo {
    QRect geometry;
    QRect available;
    double systemScale = 1.0;
    bool primary = false;
};

/// 啟動時主視窗會開在哪個螢幕：記住的視窗左上角（邏輯座標）落在哪個螢幕就是哪個，沒有記住
/// 或都落不到時是主螢幕。Qt 的邏輯座標以每個螢幕的左上角為原點、大小除以那個螢幕的比例，
/// 所以用上次執行的比例換回去（lastScale 0＝上次跟隨系統，用各螢幕自己的系統縮放）。
/// 回傳索引；沒有螢幕時 -1。
int startupMonitor(const QList<MonitorInfo>& monitors, std::optional<QPoint> savedPosition,
                   double lastScale);

/// 啟動時的決定。
struct UiScaleStartup {
    /// 啟動時 settings.ini 的設定值。
    QString setting = QStringLiteral("system");
    /// 這次套用的比例；0＝跟隨系統。
    double applied = 0;
    /// 記住的比例在主視窗的螢幕放不下、這次改用跟隨系統時，給狀態列的說明；其他時候空字串。
    QString notice;
};

/// 依設定與螢幕決定這次的比例（純函式）。monitor 是主視窗會開的螢幕，沒有時照設定套用。
UiScaleStartup decideUiScale(const QString& setting, const std::optional<MonitorInfo>& monitor);

/// 套用比例要設的兩個環境變數。
struct ScaleEnvironment {
    /// QT_SCREEN_SCALE_FACTORS：每個螢幕都指定同一個比例（Qt 以它取代 Windows 的縮放，不是相乘）。
    QByteArray screenFactors;
    /// QT_SCALE_FACTOR：空的表示不設。
    QByteArray globalFactor;
};

/// 比例 scale 的環境變數。Qt 6.12 不採用不大於 1 的螢幕比例（實測 1、0.75 都退回 Windows 的縮放），
/// 所以這時螢幕比例寫成 scale×2，再用 QT_SCALE_FACTOR 0.5 乘回來，結果正好是 scale。
ScaleEnvironment scaleEnvironment(double scale, int screenCount);

/// 「175%」：比例的百分比文字（四捨五入到整數，百分比與數字之間不空格）。
QString percentText(double scale);

/// 目前所有螢幕（Windows API；以每個螢幕各自 DPI 的方式取得實體像素）。
QList<MonitorInfo> currentMonitors();

/// 視窗目前所在的螢幕；hwnd 是 0 或取不到時是主螢幕，再取不到時沒有值。
std::optional<MonitorInfo> monitorOfWindow(quintptr hwnd);

/// 記下這次實際套用的比例（ui/appliedScale；0＝跟隨系統，含放不下、這次改跟隨系統的情況）。
/// 要和 ui/windowX、windowY 一起寫：那兩個是用這個比例換算的邏輯座標。
void saveAppliedUiScale(const QString& settingsPath, double applied);

/// 關閉主視窗時記比例：positionSaved 是這次關閉有沒有一起記下視窗位置（主視窗只在一般視窗時記，
/// 最大化、全螢幕、最小化時不動）。位置沒寫時比例也不寫，否則舊位置會被當成新比例的座標。
void saveAppliedUiScaleOnClose(const QString& settingsPath, double applied, bool positionSaved);

/// 上次記下的實際比例；沒有記錄時（之前的版本沒有介面縮放，都是跟隨系統）是 0。
double lastAppliedUiScale(const QString& settingsPath);

/// applyUiScaleBeforeApp 的決定（螢幕由呼叫端給，測試用替身）：記住的視窗位置用上次實際套用的
/// 比例認出螢幕，再用這次的設定判斷那個螢幕放不放得下。
UiScaleStartup decideStartupUiScale(const QString& settingsPath,
                                    const QList<MonitorInfo>& monitors);

/// 在建立 QGuiApplication 之前呼叫一次：讀 settingsPath 的 ui/scale 與記住的視窗位置，
/// 決定比例，設好環境變數 QT_SCREEN_SCALE_FACTORS 與捨入策略（PassThrough，125%、175% 不被
/// 四捨五入）。settingsPath 空字串時（還沒決定資料夾）只設捨入策略、跟隨系統。
/// 結果也記下來，之後由 startupUiScale() 取（主視窗與設定頁用）。
UiScaleStartup applyUiScaleBeforeApp(const QString& settingsPath);

/// applyUiScaleBeforeApp 的結果；沒有呼叫過時是跟隨系統。
const UiScaleStartup& startupUiScale();

} // namespace auralis::core
