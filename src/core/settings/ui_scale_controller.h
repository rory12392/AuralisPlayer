#pragma once

#include "settings/ui_scale.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

#include <functional>
#include <optional>

namespace auralis::core {

/// 設定頁「外觀」的介面縮放（S-16，AC-19-4）：QML 以單例 UiScale 取用。
/// 選了立即寫進 ui/scale，重新啟動後生效（這次啟動的比例不變）。每個比例依主視窗目前所在
/// 螢幕的可用範圍（實體像素）判斷放不放得下；文字由畫面組。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class UiScaleController : public QObject {
    Q_OBJECT
    /// 設定值（system、1.0、1.25、1.5、1.75、2.0）；寫入時存進 settings.ini。
    Q_PROPERTY(QString value READ value WRITE setValue NOTIFY valueChanged)
    /// 這次啟動時的設定值；和 value 不同時畫面顯示「重新啟動後生效。」
    Q_PROPERTY(QString startupValue READ startupValue CONSTANT)
    /// 依選單順序：[{value, scale（system 是 0）, fits, width, height（最小視窗的實體像素）}]。
    Q_PROPERTY(QVariantList options READ options NOTIFY screenChanged)
    /// 主視窗所在螢幕的 Windows 縮放（1.5＝150%）與可用範圍（實體像素）。
    Q_PROPERTY(double systemScale READ systemScale NOTIFY screenChanged)
    Q_PROPERTY(QSize available READ available NOTIFY screenChanged)

public:
    /// 取主視窗目前所在的螢幕（主程式接 Windows API，測試換成假的）。
    using MonitorSource = std::function<std::optional<MonitorInfo>()>;

    UiScaleController(QString settingsPath, QString startupValue, MonitorSource monitor,
                      QObject* parent = nullptr);

    QString value() const { return m_value; }
    void setValue(const QString& value);
    QString startupValue() const { return m_startupValue; }
    QVariantList options() const;
    double systemScale() const { return m_monitor ? m_monitor->systemScale : 1.0; }
    QSize available() const { return m_monitor ? m_monitor->available.size() : QSize(); }

    /// 重新取主視窗所在的螢幕（設定頁開啟、主視窗換到別的螢幕時呼叫）。
    Q_INVOKABLE void refresh();

    static void setInstance(UiScaleController* instance);
    static UiScaleController* instance();

signals:
    void valueChanged();
    void screenChanged();

private:
    QString m_settingsPath;
    QString m_value;
    QString m_startupValue;
    MonitorSource m_source;
    std::optional<MonitorInfo> m_monitor;
};

} // namespace auralis::core
