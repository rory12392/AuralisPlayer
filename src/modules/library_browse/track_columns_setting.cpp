#include "track_columns_setting.h"

#include <auralis/module_settings.h>

#include <QJSEngine>

namespace auralis::mod::lib {

namespace {
TrackColumnsSetting* g_instance = nullptr;
const QString kTrackColumnsKey = QStringLiteral("trackColumns");
} // namespace

TrackColumnsSetting::TrackColumnsSetting(ModuleSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
    if (m_settings) {
        m_saved = m_settings->value(kTrackColumnsKey).toString();
    }
}

void TrackColumnsSetting::setSaved(const QString& value) {
    if (value == m_saved) {
        return;
    }
    m_saved = value;
    if (m_settings) {
        m_settings->setValue(kTrackColumnsKey, m_saved);
    }
    emit savedChanged();
}

void TrackColumnsSetting::setInstance(TrackColumnsSetting* instance) {
    g_instance = instance;
}

TrackColumnsSetting* TrackColumnsSetting::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "TrackColumnsSetting", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib
