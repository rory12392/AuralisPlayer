#pragma once

#include "plugins/plugin_registry_impl.h"
#include "plugins/plugin_status.h"

#include <auralis/result.h>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

namespace auralis::core {

/// 官方外掛：id 與這個版本的 DLL 的 SHA-256（小寫十六進位）。主程式建置時產生（officialPlugins）。
struct OfficialPlugin {
    QString id;
    QByteArray sha256;
};

/// 和主程式同一次建置的官方外掛清單（建置時由 CMake 產生；沒有建置任何外掛時是空的）。
std::vector<OfficialPlugin> officialPlugins();

/// 外掛管理：使用者的外掛放在 <資料夾>\plugins\<id>\（plugin.json＋DLL）；隨主程式安裝的官方外掛
/// 放在程式資料夾的 plugins\<id>\（內建，唯讀；只看官方外掛的 id，Qt 的外掛資料夾不算，T-105）。
/// 程式啟動時（模組啟動前）loadAll 一次：先套用上次的匯入與移除，再逐一驗證
/// （plugin.json、類型、版本、官方雜湊）並載入；任何一個外掛失敗只影響它自己。
/// 同一個 id 兩邊都有時，使用者那份能用（通過驗證，或被停用）就用它，不能用時改用內建的。
/// 內建外掛預設啟用、可以停用，不能移除（只能移除使用者匯入的那份）。
/// 匯入、移除、停用都只記下來，重新啟動後生效（回傳 true 表示需要重新啟動）：
/// 載入中的 DLL 不能覆蓋或刪除，模組也只在啟動時取外掛。
/// 停用清單存在 settings.ini 的 plugins/disabled，待移除的在 plugins/pendingRemove；
/// 匯入的外掛先放在 plugins\.staging\<id>\。不從網路下載任何東西。
/// 執行緒：只在 UI 執行緒使用。
class PluginManager {
public:
    /// builtinDir：程式資料夾的 plugins\（空字串表示沒有內建外掛）。
    PluginManager(QString dataDir, QString settingsPath, QString appVersion,
                  std::vector<OfficialPlugin> official, QString builtinDir = {});

    /// 套用待辦、驗證並載入；已啟用的外掛加進 registry。只呼叫一次。
    void loadAll(PluginRegistryImpl& registry);

    /// 已安裝與待安裝的外掛（依 id 排序），含重新啟動後才生效的操作。
    QList<PluginStatus> list() const;

    /// 匯入：manifestPath 是外掛資料夾裡的 plugin.json（DLL 在它旁邊）。驗證格式、類型、版本、
    /// 官方雜湊後複製到 plugins\.staging\<id>\（先寫暫存資料夾再改名，不會留下半份）。
    /// 驗證不過時回錯，什麼都不寫。不執行外掛的任何程式碼。
    Result<bool> importPlugin(const QString& manifestPath);
    /// 移除：重新啟動時刪掉外掛資料夾（還沒安裝的匯入直接刪掉）。
    Result<bool> remove(const QString& id);
    /// 停用或啟用（記在停用清單）。
    Result<bool> setEnabled(const QString& id, bool enabled);

    QString pluginsDir() const { return m_pluginsDir; }

private:
    QStringList settingsList(const QString& key) const;
    void setSettingsList(const QString& key, const QStringList& values) const;
    /// 套用上次的移除與匯入（啟動時，任何 DLL 都還沒載入）。
    void applyPending() const;
    /// 驗證一個外掛資料夾、通過的話載入。
    PluginStatus loadFolder(const QString& folder, const QStringList& disabled,
                            PluginRegistryImpl& registry) const;
    /// 版本與類型的檢查（不碰 DLL）；不通過時設好 state 與 reason 並回傳 false。
    bool checkCompatible(PluginStatus& status, int abi, const QString& appMin,
                         const QString& appMax) const;
    QByteArray officialHash(const QString& id) const;
    /// 程式資料夾裡這個 id 的內建外掛資料夾；沒有時是空字串。
    QString builtinFolder(const QString& id) const;

    QString m_pluginsDir;
    QString m_builtinDir;
    QString m_settingsPath;
    QString m_appVersion;
    std::vector<OfficialPlugin> m_official;
    /// loadAll 時每個外掛的狀態，和那時的停用清單（算「重新啟動後生效」用）。
    QList<PluginStatus> m_loaded;
    QStringList m_disabledAtLoad;
};

} // namespace auralis::core
