#pragma once

#include "modules/core_services.h"
#include "modules/module_registry.h"

#include <QString>

#include <memory>
#include <vector>

namespace auralis::core {

struct ModuleStatus {
    enum class State { Started, Disabled, Failed };
    QString code;
    QString name;
    QString version;
    State state = State::Disabled;
    /// 只在 Failed 時有內容（給日誌與自我檢查；介面上的提示由 UI 另外組）。
    QString error;
};

/// 模組宿主：依探索結果建立模組、依設定決定是否啟動，並在 start() 外圍做錯誤邊界。
/// 只在 UI 執行緒使用。
class ModuleManager {
public:
    /// settingsPath 是 settings.ini；modules/<代碼>/enabled 為 false 的模組不啟動。
    ModuleManager(CoreServices& services, QString settingsPath,
                  std::vector<ModuleRegistration> modules = registeredModules());
    ~ModuleManager();

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    void startAll();
    /// 依啟動的相反順序呼叫 stop()；可以重複呼叫。
    void stopAll() noexcept;

    const std::vector<ModuleStatus>& statuses() const { return m_statuses; }

private:
    struct Entry;
    CoreServices& m_services;
    QString m_settingsPath;
    std::vector<ModuleRegistration> m_registrations;
    std::vector<std::unique_ptr<Entry>> m_started;
    std::vector<ModuleStatus> m_statuses;
};

QString toString(ModuleStatus::State state);

} // namespace auralis::core
