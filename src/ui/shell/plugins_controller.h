#pragma once

#include "plugins/plugin_manager.h"

#include <auralis/dialogs.h>
#include <auralis/result.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QQmlEngine;
class QJSEngine;

namespace auralis::ui {

/// 外掛管理的四個操作（主程式接 core::PluginManager，測試換成假的）。
struct PluginsBackend {
    std::function<QList<core::PluginStatus>()> list;
    std::function<Result<bool>(const QString& manifestPath)> importPlugin;
    std::function<Result<bool>(const QString& id)> remove;
    std::function<Result<bool>(const QString& id, bool enabled)> setEnabled;

    /// 接到 PluginManager（它要活得比控制器久）。
    static PluginsBackend from(core::PluginManager& manager);
};

/// 設定頁「外掛」分區（S-16，AC-03-0，T-104）的資料：QML 以單例 Plugins 取用。
/// 清單每列：名稱、類型、版本、狀態、啟用開關、移除；官方 ASIO 外掛那一列一定在
/// （沒安裝時是「未安裝」的佔位列），另外帶 ASIO 的狀態句。匯入、移除、停用都經外掛管理，
/// 結果都是「重新啟動後生效」（程式沒有自己重新啟動的機制，只顯示提示）。
/// 不從網路下載任何東西；Release 頁只交給瀏覽器開。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PluginsController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Plugins)
    QML_SINGLETON
    /// [{id, name, typeText, version, stateText, tone, pendingText, enabled, canToggle, canRemove,
    ///   builtin, asio, asioStatus}]；tone 是 StatusTag 的 variant；builtin（隨主程式安裝）的
    ///   沒有移除，只能停用。
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    /// 有操作要重新啟動後才生效。
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    /// 上一個操作的結果（成功是「重新啟動後生效。」，失敗是原因）；沒有時空字串。
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY changed)
    /// 匯入對話框的起始資料夾：「下載」（不是音樂資料夾）。
    Q_PROPERTY(QUrl dialogFolder READ dialogFolder CONSTANT)
    /// ASIO 外掛的下載頁（GitHub Release）。
    Q_PROPERTY(QString releaseUrl READ releaseUrl CONSTANT)
    /// ASIO Compatible 標誌：從已載入的 ASIO 外掛資料夾讀（plugin.json 的 logo，T-105），
    /// 深色主題用 asioLogoDark、淺色用 asioLogoLight；
    /// 外掛沒載入或沒有標誌檔時是空的，畫面改用文字標示。
    Q_PROPERTY(QUrl asioLogoDark READ asioLogoDark NOTIFY changed)
    Q_PROPERTY(QUrl asioLogoLight READ asioLogoLight NOTIFY changed)

public:
    /// 官方 ASIO 外掛的 id（plugin.json 的 id、plugins\ 底下的資料夾名，T-103 D-127）。
    static constexpr const char* kAsioId = "AuralisAsio";

    /// dialogs 為 nullptr 時移除不問、直接做（測試用）。
    PluginsController(PluginsBackend backend, Dialogs* dialogs, QObject* parent = nullptr);

    QVariantList rows() const;
    bool restartNeeded() const;
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    QUrl dialogFolder() const;
    QString releaseUrl() const;
    QUrl asioLogoDark() const;
    QUrl asioLogoLight() const;

    /// 匯入檔案對話框選的檔：外掛資料夾裡的 plugin.json 或 DLL（DLL 時用它旁邊的 plugin.json）。
    Q_INVOKABLE void importFile(const QUrl& file);
    /// 先問「移除外掛 <名稱>」，確認後記下來，重新啟動時刪掉。
    Q_INVOKABLE void confirmRemove(const QString& id);
    Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
    /// 用預設瀏覽器開 Release 頁（程式本身不下載）。
    Q_INVOKABLE void openReleasePage();

    /// 測試換掉「開瀏覽器」；空的 function 還原。
    static void setOpenUrlHook(std::function<bool(const QUrl&)> hook);
    static void setInstance(PluginsController* instance);
    static PluginsController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    void finish(const Result<bool>& result);
    void remove(const QString& id);
    /// 已載入的 ASIO 外掛的標誌檔（dark 為 true 時是深色主題用的）；沒有時是空的。
    QUrl asioLogo(bool dark) const;

    PluginsBackend m_backend;
    Dialogs* m_dialogs;
    QString m_message;
    bool m_messageIsError = false;
};

} // namespace auralis::ui
