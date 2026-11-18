#pragma once

#include <QString>
#include <QVariant>

namespace auralis {

/// 讀寫 settings.ini 裡屬於這個模組的段落（<模組代碼>/<鍵>），鍵名用 camelCase，例：online。
/// 模組看不到別的段落；模組是否啟用（modules/<代碼>/enabled）由核心管理，不在這裡。
/// 執行緒：可以在任何執行緒呼叫；寫入立刻生效並寫回檔案。
class ModuleSettings {
public:
    virtual ~ModuleSettings() = default;
    ModuleSettings(const ModuleSettings&) = delete;
    ModuleSettings& operator=(const ModuleSettings&) = delete;

    /// 讀取；沒有這個鍵時回傳 defaultValue。
    virtual QVariant value(const QString& key, const QVariant& defaultValue = {}) const = 0;
    virtual void setValue(const QString& key, const QVariant& value) = 0;
    virtual bool contains(const QString& key) const = 0;
    virtual void remove(const QString& key) = 0;

protected:
    ModuleSettings() = default;
};

} // namespace auralis
