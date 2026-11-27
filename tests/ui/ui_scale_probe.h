#pragma once

// 設定頁的介面縮放（tst_ui_scale，AC-19-4）：QML 單例 UiScale 背後是真的 UiScaleController，
// 主視窗所在的螢幕換成假的（uiScaleProbe.setMonitor），設定寫在測試暫存的 settings.ini。
// 不讀也不改 Windows 的顯示設定。
#include "settings/ui_scale_controller.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QtGlobal>

#include <memory>

namespace auralis::test {

class UiScaleProbe : public QObject {
    Q_OBJECT

public:
    /// 每個測試檔的引擎各一份，跟著引擎解構；startupValue 固定是 system（這次啟動跟隨系統）。
    /// 設定檔放在這個 QML 檔自己的狀態資料夾（ctest 每個檔一個行程、平行執行）。
    static void install(QQmlEngine* engine) {
        const QString dir =
            qEnvironmentVariable("AURALIS_QUICK_STATE_DIR", QString::fromUtf8(AURALIS_TEST_TMP));
        auto* probe = new UiScaleProbe(dir + QStringLiteral("/ui-scale-settings.ini"), engine);
        engine->rootContext()->setContextProperty(QStringLiteral("uiScaleProbe"), probe);
    }

    /// 換主視窗所在的螢幕（系統縮放、可用範圍的實體像素）並讓控制器重新判斷。
    Q_INVOKABLE void setMonitor(double systemScale, int availableWidth, int availableHeight) {
        core::MonitorInfo monitor;
        monitor.systemScale = systemScale;
        monitor.available = QRect(0, 0, availableWidth, availableHeight);
        monitor.geometry = monitor.available;
        m_monitor = monitor;
        m_controller->refresh();
    }

private:
    UiScaleProbe(const QString& settingsPath, QObject* parent)
        : QObject(parent),
          m_controller(std::make_unique<core::UiScaleController>(
              settingsPath, QStringLiteral("system"), [this] { return m_monitor; })) {
        core::UiScaleController::setInstance(m_controller.get());
    }

    std::optional<core::MonitorInfo> m_monitor;
    std::unique_ptr<core::UiScaleController> m_controller;
};

} // namespace auralis::test
