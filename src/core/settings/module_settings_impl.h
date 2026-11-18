#pragma once

#include <auralis/module_settings.h>

#include <QString>

namespace auralis::core {

/// ModuleSettings 的實作：settings.ini 的 <模組代碼>/ 段。
/// 每次呼叫建立自己的 QSettings；同一個行程裡的 QSettings 共用快取，跨執行緒是安全的。
class ModuleSettingsImpl final : public ModuleSettings {
public:
    ModuleSettingsImpl(QString settingsPath, QString moduleCode);

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const override;
    void setValue(const QString& key, const QVariant& value) override;
    bool contains(const QString& key) const override;
    void remove(const QString& key) override;

private:
    QString fullKey(const QString& key) const;

    QString m_settingsPath;
    QString m_code;
};

} // namespace auralis::core
