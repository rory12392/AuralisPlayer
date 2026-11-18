#include "settings/module_settings_impl.h"

#include <QSettings>

namespace auralis::core {

ModuleSettingsImpl::ModuleSettingsImpl(QString settingsPath, QString moduleCode)
    : m_settingsPath(std::move(settingsPath)), m_code(std::move(moduleCode)) {}

QString ModuleSettingsImpl::fullKey(const QString& key) const {
    return m_code + QLatin1Char('/') + key;
}

QVariant ModuleSettingsImpl::value(const QString& key, const QVariant& defaultValue) const {
    return QSettings(m_settingsPath, QSettings::IniFormat).value(fullKey(key), defaultValue);
}

void ModuleSettingsImpl::setValue(const QString& key, const QVariant& value) {
    QSettings(m_settingsPath, QSettings::IniFormat).setValue(fullKey(key), value);
}

bool ModuleSettingsImpl::contains(const QString& key) const {
    return QSettings(m_settingsPath, QSettings::IniFormat).contains(fullKey(key));
}

void ModuleSettingsImpl::remove(const QString& key) {
    QSettings(m_settingsPath, QSettings::IniFormat).remove(fullKey(key));
}

} // namespace auralis::core
