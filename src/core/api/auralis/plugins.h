// 外掛：類型與「已載入的外掛」查詢（模組從 ModuleHost::plugins() 取得自己類型的外掛）。
#pragma once

#include <QList>
#include <QString>

namespace auralis {

/// 外掛類型。架構預留多種，這一版只接受 Output（其他類型載入時拒絕，狀態「這一版不支援」）。
/// 每種類型由一個模組接手（pluginTypeOwner）：模組啟動時向 PluginRegistry 要自己類型的外掛。
enum class PluginType {
    Output,  ///< 輸出路徑（"output"，OUT 接手；第一個是 ASIO 輸出）
    Decoder, ///< 解碼（"decoder"，下一版）
    Dsp,     ///< 訊號處理（"dsp"，下一版）
    Ui,      ///< 介面（"ui"，下一版）
};

/// plugin.json 的 type 字串。
inline QString pluginTypeName(PluginType type) {
    switch (type) {
    case PluginType::Decoder:
        return QStringLiteral("decoder");
    case PluginType::Dsp:
        return QStringLiteral("dsp");
    case PluginType::Ui:
        return QStringLiteral("ui");
    case PluginType::Output:
        break;
    }
    return QStringLiteral("output");
}

/// type 字串 → 類型；不認得的字串回傳 false。
inline bool pluginTypeFromString(const QString& text, PluginType& type) {
    for (const PluginType candidate :
         {PluginType::Output, PluginType::Decoder, PluginType::Dsp, PluginType::Ui}) {
        if (text == pluginTypeName(candidate)) {
            type = candidate;
            return true;
        }
    }
    return false;
}

/// 這個類型由哪個模組接手（模組代碼，例 "OUT"）；這一版不支援的類型回傳空字串。
inline QString pluginTypeOwner(PluginType type) {
    return type == PluginType::Output ? QStringLiteral("OUT") : QString();
}

/// 一個已載入、啟用中的外掛。api 是外掛進入點給的、依類型的介面指標，
/// 由接手的模組轉成自己定義的介面（例：OUT 的 IOutputPlugin）；外掛 DLL
/// 到程式結束都不卸載，指標一直有效。
struct LoadedPlugin {
    QString id;
    QString name;
    QString version;
    PluginType type = PluginType::Output;
    void* api = nullptr;
};

/// 已載入的外掛（核心在模組啟動前載入完）。外掛的匯入、移除、停用由核心的外掛管理處理，
/// 重新啟動後生效，所以這裡的內容在程式執行期間不變。
/// 執行緒：只在 UI 執行緒使用。
class PluginRegistry {
public:
    virtual ~PluginRegistry() = default;
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    /// 這個類型已啟用的外掛（依 id 排序）；沒有時是空清單。
    virtual QList<LoadedPlugin> loaded(PluginType type) const = 0;

protected:
    PluginRegistry() = default;
};

} // namespace auralis
