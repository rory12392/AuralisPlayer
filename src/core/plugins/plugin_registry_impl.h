#pragma once

#include <auralis/plugins.h>

namespace auralis::core {

/// 已載入的外掛（外掛管理在模組啟動前填好，之後不變）。
class PluginRegistryImpl final : public PluginRegistry {
public:
    QList<LoadedPlugin> loaded(PluginType type) const override {
        QList<LoadedPlugin> result;
        for (const LoadedPlugin& plugin : m_plugins) {
            if (plugin.type == type) {
                result.append(plugin);
            }
        }
        return result;
    }

    /// 外掛管理呼叫（依 id 順序加入）。
    void add(LoadedPlugin plugin) { m_plugins.append(std::move(plugin)); }

private:
    QList<LoadedPlugin> m_plugins;
};

} // namespace auralis::core
