#include "shell/plugins_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QVariantMap>

#include <utility>

namespace auralis::ui {

namespace {

PluginsController* g_instance = nullptr;
std::function<bool(const QUrl&)> g_openUrlHook;

using State = core::PluginStatus::State;
using Pending = core::PluginStatus::Pending;

QString stateText(const core::PluginStatus& status) {
    switch (status.state) {
    case State::Enabled:
        return PluginsController::tr("已啟用");
    case State::Disabled:
        return PluginsController::tr("已停用");
    case State::VersionMismatch:
        return PluginsController::tr("版本不符，請更新");
    case State::FileMismatch:
        return PluginsController::tr("檔案不符");
    case State::UnsupportedType:
        return PluginsController::tr("這一版不支援");
    case State::NotInstalled:
        return PluginsController::tr("重新啟動後安裝");
    case State::Invalid:
    case State::LoadFailed:
        break;
    }
    return status.reason.isEmpty() ? PluginsController::tr("無法載入") : status.reason;
}

/// StatusTag 的 variant：能用的是 neutral、停用是 idle、不能載入的是 signal（琥珀只給位元完美）。
QString tone(const core::PluginStatus& status) {
    switch (status.state) {
    case State::Enabled:
    case State::NotInstalled:
        return QStringLiteral("neutral");
    case State::Disabled:
        return QStringLiteral("idle");
    default:
        return QStringLiteral("signal");
    }
}

QString pendingText(Pending pending) {
    switch (pending) {
    case Pending::Install:
        return PluginsController::tr("重新啟動後安裝");
    case Pending::Remove:
        return PluginsController::tr("重新啟動後移除");
    case Pending::Enable:
        return PluginsController::tr("重新啟動後啟用");
    case Pending::Disable:
        return PluginsController::tr("重新啟動後停用");
    case Pending::None:
        break;
    }
    return {};
}

QString typeText(const QString& type) {
    return type == QLatin1String("output") ? PluginsController::tr("輸出") : type;
}

/// ASIO 外掛那一列的狀態句（未安裝／已安裝（版本）／版本不符／檔案不符或損毀（原因））。
QString asioStatus(const core::PluginStatus* status) {
    if (status == nullptr) {
        return PluginsController::tr("未安裝");
    }
    switch (status->state) {
    case State::Enabled:
        return PluginsController::tr("已安裝（%1）").arg(status->version);
    case State::Disabled:
        return PluginsController::tr("已安裝（%1），已停用").arg(status->version);
    case State::NotInstalled:
        return PluginsController::tr("已匯入（%1），重新啟動後安裝").arg(status->version);
    case State::VersionMismatch:
        return PluginsController::tr("版本不符，請下載和目前版本相同的外掛");
    case State::UnsupportedType:
        return status->reason;
    case State::FileMismatch:
    case State::Invalid:
    case State::LoadFailed:
        break;
    }
    return PluginsController::tr("檔案不符或損毀（%1）")
        .arg(status->reason.isEmpty() ? stateText(*status) : status->reason);
}

/// 第一次出現的「ASIO」加 ®（Steinberg 商標規則第 15 條）。
QString withTrademark(QString name) {
    const qsizetype at = name.indexOf(QLatin1String("ASIO"));
    if (at >= 0 && name.mid(at + 4, 1) != QStringLiteral("®")) {
        name.insert(at + 4, QStringLiteral("®"));
    }
    return name;
}

QVariantMap row(const core::PluginStatus& status) {
    const bool installed = status.state == State::Enabled || status.state == State::Disabled;
    bool enabled = status.state == State::Enabled;
    if (status.pending == Pending::Enable) {
        enabled = true;
    } else if (status.pending == Pending::Disable) {
        enabled = false;
    }
    const bool asio = status.id == QLatin1String(PluginsController::kAsioId);
    return {
        {QStringLiteral("id"), status.id},
        {QStringLiteral("name"), asio ? withTrademark(status.name) : status.name},
        {QStringLiteral("typeText"), typeText(status.type)},
        {QStringLiteral("version"), status.version},
        {QStringLiteral("stateText"), stateText(status)},
        {QStringLiteral("tone"), tone(status)},
        {QStringLiteral("pendingText"),
         status.pending == Pending::Install ? QString() : pendingText(status.pending)},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("canToggle"), installed && status.pending != Pending::Remove},
        // 內建（隨主程式安裝）的只能停用，不能移除（T-105）
        {QStringLiteral("canRemove"), !status.builtin && status.pending != Pending::Remove},
        {QStringLiteral("builtin"), status.builtin},
        {QStringLiteral("asio"), asio},
        {QStringLiteral("asioStatus"), asio ? asioStatus(&status) : QString()},
    };
}

} // namespace

PluginsBackend PluginsBackend::from(core::PluginManager& manager) {
    return {[&manager] { return manager.list(); },
            [&manager](const QString& path) { return manager.importPlugin(path); },
            [&manager](const QString& id) { return manager.remove(id); },
            [&manager](const QString& id, bool enabled) {
                return manager.setEnabled(id, enabled);
            }};
}

PluginsController::PluginsController(PluginsBackend backend, Dialogs* dialogs, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_dialogs(dialogs) {}

QVariantList PluginsController::rows() const {
    QVariantList result;
    bool hasAsio = false;
    for (const auto& status : m_backend.list()) {
        hasAsio = hasAsio || status.id == QLatin1String(kAsioId);
        result.append(row(status));
    }
    if (!hasAsio) {
        // 官方 ASIO 外掛還沒安裝：佔位列（沒有開關與移除），帶下載說明
        result.prepend(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(kAsioId)},
            {QStringLiteral("name"), tr("ASIO® 輸出")},
            {QStringLiteral("typeText"), typeText(QStringLiteral("output"))},
            {QStringLiteral("version"), QString()},
            {QStringLiteral("stateText"), tr("未安裝")},
            {QStringLiteral("tone"), QStringLiteral("idle")},
            {QStringLiteral("pendingText"), QString()},
            {QStringLiteral("enabled"), false},
            {QStringLiteral("canToggle"), false},
            {QStringLiteral("canRemove"), false},
            {QStringLiteral("builtin"), false},
            {QStringLiteral("asio"), true},
            {QStringLiteral("asioStatus"), asioStatus(nullptr)},
        });
    }
    return result;
}

bool PluginsController::restartNeeded() const {
    for (const auto& status : m_backend.list()) {
        if (status.pending != Pending::None) {
            return true;
        }
    }
    return false;
}

QUrl PluginsController::dialogFolder() const {
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

QString PluginsController::releaseUrl() const {
    return QStringLiteral("https://github.com/rory12392/AuralisPlayer/releases");
}

QUrl PluginsController::asioLogo(bool dark) const {
    // 主程式不帶 Steinberg 的檔案：標誌跟著 ASIO 外掛（外掛資料夾裡的原檔），外掛這次有載入才顯示
    for (const auto& status : m_backend.list()) {
        if (status.id == QLatin1String(kAsioId) && status.state == State::Enabled) {
            const QString& path = dark ? status.logoDark : status.logoLight;
            return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
        }
    }
    return {};
}

QUrl PluginsController::asioLogoDark() const {
    return asioLogo(true);
}

QUrl PluginsController::asioLogoLight() const {
    return asioLogo(false);
}

void PluginsController::importFile(const QUrl& file) {
    const QFileInfo picked(file.toLocalFile());
    // 選了 DLL 時用它旁邊的 plugin.json（外掛管理以 plugin.json 驗證，T-103 D-126）
    const QString manifest = picked.suffix().compare(QLatin1String("dll"), Qt::CaseInsensitive) == 0
                                 ? picked.dir().filePath(QStringLiteral("plugin.json"))
                                 : picked.filePath();
    if (!QFileInfo(manifest).isFile()) {
        finish(Error{tr("外掛資料夾裡沒有 plugin.json，"
                        "請解開外掛的 zip 後選裡面的 plugin.json 或 DLL。")});
        return;
    }
    finish(m_backend.importPlugin(QDir::toNativeSeparators(manifest)));
}

void PluginsController::confirmRemove(const QString& id) {
    if (m_dialogs == nullptr) {
        remove(id);
        return;
    }
    QString name = id;
    for (const auto& status : m_backend.list()) {
        if (status.id == id && !status.name.isEmpty()) {
            name = status.name;
        }
    }
    m_dialogs->confirm({tr("移除外掛 %1").arg(name),
                        tr("重新啟動後外掛的檔案會從程式的外掛資料夾刪除，要用時要再匯入一次。"),
                        tr("移除外掛"), true},
                       [this, id](bool confirmed) {
                           if (confirmed) {
                               remove(id);
                           }
                       });
}

void PluginsController::remove(const QString& id) {
    finish(m_backend.remove(id));
}

void PluginsController::setEnabled(const QString& id, bool enabled) {
    finish(m_backend.setEnabled(id, enabled));
}

void PluginsController::openReleasePage() {
    const QUrl url(releaseUrl());
    if (g_openUrlHook) {
        g_openUrlHook(url);
        return;
    }
    QDesktopServices::openUrl(url);
}

void PluginsController::finish(const Result<bool>& result) {
    if (result) {
        m_message = tr("重新啟動後生效。");
        m_messageIsError = false;
    } else {
        m_message = result.error().message;
        m_messageIsError = true;
    }
    emit changed();
}

void PluginsController::setOpenUrlHook(std::function<bool(const QUrl&)> hook) {
    g_openUrlHook = std::move(hook);
}

void PluginsController::setInstance(PluginsController* instance) {
    g_instance = instance;
}

PluginsController* PluginsController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PluginsController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui
