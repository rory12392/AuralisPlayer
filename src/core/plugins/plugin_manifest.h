#pragma once

#include <auralis/result.h>

#include <QString>

namespace auralis::core {

/// 外掛的說明檔 plugin.json（每個外掛資料夾一份，和 DLL 放在一起）。全部欄位都必填：
/// { "id": "AuralisAsio", "name": "ASIO 輸出", "type": "output", "version": "0.1.0",
///   "appVersion": { "min": "0.1.0", "max": "0.1.0" }, "abi": 1, "dll": "AuralisAsio.dll",
///   "license": "GPL-3.0-only", "url": "https://…" }
struct PluginManifest {
    /// 英數字、-、_，1–64 字；外掛資料夾的名稱要等於它。
    QString id;
    QString name;
    /// 類型字串（"output" 等）；認不認得、這一版支不支援由外掛管理判斷。
    QString type;
    QString version;
    /// 需要的主程式版本範圍（含兩端）。
    QString appMin;
    QString appMax;
    int abi = 0;
    /// DLL 檔名（只有檔名，在 plugin.json 旁邊；不能帶路徑）。
    QString dll;
    QString license;
    QString url;
    /// 選填（T-105）："logo": { "dark": "logo-dark.svg", "light": "logo-light.svg" }：
    /// 深色與淺色主題用的標誌檔名（只有檔名、.svg 或 .png，在 plugin.json 旁邊）；沒寫時是空的。
    QString logoDark;
    QString logoLight;
};

/// 讀 plugin.json；格式錯或缺欄位時回錯（訊息寫給使用者看，例：「plugin.json 缺少 dll」）。
/// 不碰 DLL。
Result<PluginManifest> readPluginManifest(const QString& path);

/// 外掛資料夾裡的檔名（plugin.json 寫的，例：標誌）換成路徑；沒寫或檔案不在時是空字串。
QString pluginFilePath(const QString& folder, const QString& name);

/// id 是否可以當資料夾名稱（擋掉路徑分隔、..、保留字元）。
bool isValidPluginId(const QString& id);

} // namespace auralis::core
