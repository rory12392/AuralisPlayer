#pragma once

#include <QString>

namespace auralis::core {

/// 一個外掛的狀態（設定頁「外掛」分區與 --self-check 用）。
struct PluginStatus {
    enum class State {
        Enabled,         ///< 已啟用（這次執行已載入）
        Disabled,        ///< 使用者停用
        VersionMismatch, ///< 主程式版本或外掛介面版本不符，要更新外掛
        FileMismatch,    ///< DLL 不是這個版本的官方外掛
        UnsupportedType, ///< 這一版不支援的類型
        Invalid,         ///< plugin.json 格式錯、缺欄位、DLL 不在
        LoadFailed,      ///< 載入或初始化失敗（reason 寫原因）
        NotInstalled,    ///< 剛匯入，重新啟動後安裝
    };
    /// 已經做了、重新啟動後才生效的操作。
    enum class Pending { None, Install, Remove, Enable, Disable };

    /// plugin.json 讀不到時是資料夾名稱。
    QString id;
    QString name;
    QString type;
    QString version;
    QString license;
    QString url;
    State state = State::Invalid;
    /// 不是 Enabled、Disabled 時的原因（寫給使用者看）。
    QString reason;
    Pending pending = Pending::None;
    /// 程式資料夾裡有這個外掛（隨主程式安裝）：可以停用、不能移除（T-105）。
    bool builtin = false;
    /// 這次採用的外掛資料夾（絕對路徑；剛匯入、還沒安裝的是空的）。
    QString folder;
    /// plugin.json 的 logo 欄位指的標誌檔（絕對路徑），深色與淺色主題各一；檔案不在時是空的。
    QString logoDark;
    QString logoLight;
};

/// 外掛「能用」：通過驗證（已啟用）或被使用者停用。
/// 使用者匯入的那份能用時，同 id 的內建外掛就不用（T-105）。
inline bool isUsable(const PluginStatus& status) {
    return status.state == PluginStatus::State::Enabled ||
           status.state == PluginStatus::State::Disabled;
}

inline QString toString(PluginStatus::State state) {
    switch (state) {
    case PluginStatus::State::Enabled:
        return QStringLiteral("enabled");
    case PluginStatus::State::Disabled:
        return QStringLiteral("disabled");
    case PluginStatus::State::VersionMismatch:
        return QStringLiteral("versionMismatch");
    case PluginStatus::State::FileMismatch:
        return QStringLiteral("fileMismatch");
    case PluginStatus::State::UnsupportedType:
        return QStringLiteral("unsupportedType");
    case PluginStatus::State::Invalid:
        return QStringLiteral("invalid");
    case PluginStatus::State::LoadFailed:
        return QStringLiteral("loadFailed");
    case PluginStatus::State::NotInstalled:
        return QStringLiteral("notInstalled");
    }
    return {};
}

inline QString toString(PluginStatus::Pending pending) {
    switch (pending) {
    case PluginStatus::Pending::Install:
        return QStringLiteral("install");
    case PluginStatus::Pending::Remove:
        return QStringLiteral("remove");
    case PluginStatus::Pending::Enable:
        return QStringLiteral("enable");
    case PluginStatus::Pending::Disable:
        return QStringLiteral("disable");
    case PluginStatus::Pending::None:
        break;
    }
    return QStringLiteral("none");
}

} // namespace auralis::core
