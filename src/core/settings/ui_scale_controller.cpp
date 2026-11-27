#include "settings/ui_scale_controller.h"

#include <QVariantMap>

namespace auralis::core {

namespace {
UiScaleController* g_instance = nullptr;
}

UiScaleController::UiScaleController(QString settingsPath, QString startupValue,
                                     MonitorSource monitor, QObject* parent)
    : QObject(parent), m_settingsPath(std::move(settingsPath)),
      m_value(savedUiScale(m_settingsPath)), m_startupValue(std::move(startupValue)),
      m_source(std::move(monitor)) {
    refresh();
}

void UiScaleController::setValue(const QString& value) {
    if (value == m_value || !saveUiScale(m_settingsPath, value)) {
        return;
    }
    m_value = value;
    emit valueChanged();
}

QVariantList UiScaleController::options() const {
    QVariantList list;
    for (const QString& value : uiScaleValues()) {
        const double scale = uiScaleFactor(value);
        const QSize needed = minWindowPhysical(scale > 0 ? scale : systemScale());
        // 取不到螢幕時不停用任何選項
        const bool fits = scale <= 0 || !m_monitor || uiScaleFits(scale, available());
        list.append(QVariantMap{{QStringLiteral("value"), value},
                                {QStringLiteral("scale"), scale},
                                {QStringLiteral("fits"), fits},
                                {QStringLiteral("width"), needed.width()},
                                {QStringLiteral("height"), needed.height()}});
    }
    return list;
}

void UiScaleController::refresh() {
    m_monitor = m_source ? m_source() : std::nullopt;
    emit screenChanged();
}

void UiScaleController::setInstance(UiScaleController* instance) {
    g_instance = instance;
}

UiScaleController* UiScaleController::instance() {
    return g_instance;
}

} // namespace auralis::core
