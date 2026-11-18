#include "plugins/plugin_manager.h"

#include "plugins/plugin_loader.h"
#include "plugins/plugin_manifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QVersionNumber>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPlugins, "auralis.plugins")

namespace auralis::core {

namespace {

const QString kDisabledKey = QStringLiteral("plugins/disabled");
const QString kPendingRemoveKey = QStringLiteral("plugins/pendingRemove");
const QString kManifestName = QStringLiteral("plugin.json");
const QString kStagingName = QStringLiteral(".staging");

Error manageError(const QString& message, const char* call) {
    return Error{message, 0, QString::fromLatin1(call)};
}

/// 外掛資料夾底下的子資料夾（. 開頭的是管理用的，不算外掛），依名稱排序。
QStringList pluginFolders(const QString& dir) {
    QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    names.erase(
        std::remove_if(names.begin(), names.end(),
                       [](const QString& name) { return name.startsWith(QLatin1Char('.')); }),
        names.end());
    return names;
}

void sortById(QList<PluginStatus>& list) {
    std::sort(list.begin(), list.end(),
              [](const PluginStatus& a, const PluginStatus& b) { return a.id < b.id; });
}

} // namespace

PluginManager::PluginManager(QString dataDir, QString settingsPath, QString appVersion,
                             std::vector<OfficialPlugin> official, QString builtinDir)
    : m_pluginsDir(QDir::cleanPath(dataDir + QStringLiteral("/plugins"))),
      m_builtinDir(builtinDir.isEmpty() ? QString() : QDir::cleanPath(builtinDir)),
      m_settingsPath(std::move(settingsPath)), m_appVersion(std::move(appVersion)),
      m_official(std::move(official)) {}

QString PluginManager::builtinFolder(const QString& id) const {
    // 只看官方外掛的 id：程式資料夾的 plugins\ 也放 Qt 的外掛（platforms、tls…）
    if (m_builtinDir.isEmpty() || officialHash(id).isEmpty() || !isValidPluginId(id)) {
        return {};
    }
    const QString folder = m_builtinDir + QLatin1Char('/') + id;
    return QFileInfo(folder).isDir() ? folder : QString();
}

QStringList PluginManager::settingsList(const QString& key) const {
    const QSettings settings(m_settingsPath, QSettings::IniFormat);
    return settings.value(key).toStringList();
}

void PluginManager::setSettingsList(const QString& key, const QStringList& values) const {
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    if (values.isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key, values);
    }
}

QByteArray PluginManager::officialHash(const QString& id) const {
    for (const OfficialPlugin& plugin : m_official) {
        if (plugin.id == id) {
            return plugin.sha256;
        }
    }
    return {};
}

void PluginManager::applyPending() const {
    const QString staging = m_pluginsDir + QLatin1Char('/') + kStagingName;
    QStringList keep;
    for (const QString& id : settingsList(kPendingRemoveKey)) {
        if (!isValidPluginId(id)) {
            continue;
        }
        QDir(staging + QLatin1Char('/') + id).removeRecursively();
        QDir folder(m_pluginsDir + QLatin1Char('/') + id);
        if (folder.exists() && !folder.removeRecursively()) {
            qCWarning(lcPlugins).noquote() << "移除外掛失敗，下次啟動再試：" << id;
            keep.append(id);
        } else {
            qCInfo(lcPlugins).noquote() << "已移除外掛：" << id;
        }
    }
    setSettingsList(kPendingRemoveKey, keep);

    // 匯入：先把舊的改名留著，新的換上去成功才刪舊的；失敗時換回舊的
    for (const QString& name : QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString source = staging + QLatin1Char('/') + name;
        if (!isValidPluginId(name)) {
            QDir(source).removeRecursively(); // 寫到一半的 <id>.tmp
            continue;
        }
        const QString target = m_pluginsDir + QLatin1Char('/') + name;
        const QString old = m_pluginsDir + QStringLiteral("/.old-") + name;
        QDir(old).removeRecursively();
        const bool hadOld = QFileInfo::exists(target);
        if (hadOld && !QDir().rename(target, old)) {
            qCWarning(lcPlugins).noquote() << "安裝外掛失敗（舊版換不掉），下次啟動再試：" << name;
            continue;
        }
        if (!QDir().rename(source, target)) {
            qCWarning(lcPlugins).noquote() << "安裝外掛失敗，下次啟動再試：" << name;
            if (hadOld) {
                QDir().rename(old, target);
            }
            continue;
        }
        QDir(old).removeRecursively();
        qCInfo(lcPlugins).noquote() << "已安裝外掛：" << name;
    }
}

bool PluginManager::checkCompatible(PluginStatus& status, int abi, const QString& appMin,
                                    const QString& appMax) const {
    PluginType type{};
    if (!pluginTypeFromString(status.type, type) || pluginTypeOwner(type).isEmpty()) {
        status.state = PluginStatus::State::UnsupportedType;
        status.reason = QStringLiteral("這一版不支援「%1」類型的外掛").arg(status.type);
        return false;
    }
    if (abi != static_cast<int>(AURALIS_PLUGIN_ABI_VERSION)) {
        status.state = PluginStatus::State::VersionMismatch;
        status.reason = QStringLiteral("外掛介面版本 %1，這一版需要 %2，請更新外掛")
                            .arg(abi)
                            .arg(AURALIS_PLUGIN_ABI_VERSION);
        return false;
    }
    const QVersionNumber app = QVersionNumber::fromString(m_appVersion);
    const QVersionNumber min = QVersionNumber::fromString(appMin);
    const QVersionNumber max = QVersionNumber::fromString(appMax);
    if (min.isNull() || max.isNull() || app < min || app > max) {
        status.state = PluginStatus::State::VersionMismatch;
        status.reason = QStringLiteral("外掛是給主程式 %1–%2 的，這一版是 %3，請下載相符版本的外掛")
                            .arg(appMin, appMax, m_appVersion);
        return false;
    }
    return true;
}

PluginStatus PluginManager::loadFolder(const QString& folder, const QStringList& disabled,
                                       PluginRegistryImpl& registry) const {
    PluginStatus status;
    status.id = QFileInfo(folder).fileName();
    status.folder = folder;
    const auto manifest = readPluginManifest(folder + QLatin1Char('/') + kManifestName);
    if (!manifest) {
        status.state = PluginStatus::State::Invalid;
        status.reason = manifest.error().message;
        return status;
    }
    const PluginManifest& m = manifest.value();
    status.name = m.name;
    status.type = m.type;
    status.version = m.version;
    status.license = m.license;
    status.url = m.url;
    status.logoDark = pluginFilePath(folder, m.logoDark);
    status.logoLight = pluginFilePath(folder, m.logoLight);
    if (m.id != status.id) {
        status.state = PluginStatus::State::Invalid;
        status.reason = QStringLiteral("plugin.json 的 id（%1）和資料夾名稱不同").arg(m.id);
        return status;
    }
    if (!checkCompatible(status, m.abi, m.appMin, m.appMax)) {
        return status;
    }
    if (disabled.contains(m.id)) {
        status.state = PluginStatus::State::Disabled;
        return status;
    }
    const QString dll = folder + QLatin1Char('/') + m.dll;
    if (!QFileInfo(dll).isFile()) {
        status.state = PluginStatus::State::Invalid;
        status.reason = QStringLiteral("找不到外掛的 DLL：%1").arg(m.dll);
        return status;
    }
    const PluginLoadResult loaded = loadPluginDll(dll, officialHash(m.id));
    switch (loaded.kind) {
    case PluginLoadResult::Kind::HashMismatch:
        status.state = PluginStatus::State::FileMismatch;
        status.reason = loaded.reason;
        return status;
    case PluginLoadResult::Kind::AbiMismatch:
        status.state = PluginStatus::State::VersionMismatch;
        status.reason = loaded.reason;
        return status;
    case PluginLoadResult::Kind::Failed:
        status.state = PluginStatus::State::LoadFailed;
        status.reason = loaded.reason;
        return status;
    case PluginLoadResult::Kind::Loaded:
        break;
    }
    PluginType type{};
    pluginTypeFromString(m.type, type);
    if (QString::fromUtf8(loaded.entry->type) != m.type) {
        // api 指標的型別由類型決定，對不上就不能交給模組；官方外掛不會發生。DLL 已載入，不使用它
        status.state = PluginStatus::State::LoadFailed;
        status.reason = QStringLiteral("外掛回報的類型（%1）和 plugin.json 不同")
                            .arg(QString::fromUtf8(loaded.entry->type));
        return status;
    }
    registry.add({m.id, m.name, m.version, type, loaded.entry->api});
    status.state = PluginStatus::State::Enabled;
    return status;
}

void PluginManager::loadAll(PluginRegistryImpl& registry) {
    applyPending();
    m_disabledAtLoad = settingsList(kDisabledKey);
    m_loaded.clear();
    QStringList ids = pluginFolders(m_pluginsDir);
    for (const OfficialPlugin& plugin : m_official) {
        if (!ids.contains(plugin.id) && !builtinFolder(plugin.id).isEmpty()) {
            ids.append(plugin.id);
        }
    }
    for (const QString& name : ids) {
        const QString builtin = builtinFolder(name);
        const QString user = m_pluginsDir + QLatin1Char('/') + name;
        PluginStatus status;
        if (QFileInfo(user).isDir()) {
            status = loadFolder(user, m_disabledAtLoad, registry);
            if (!builtin.isEmpty() && !isUsable(status)) {
                qCWarning(lcPlugins).noquote() << "使用者匯入的外掛不能用，改用內建的：" << name
                                               << toString(status.state) << status.reason;
            }
        }
        if (!builtin.isEmpty() && (status.folder.isEmpty() || !isUsable(status))) {
            status = loadFolder(builtin, m_disabledAtLoad, registry);
        }
        status.builtin = !builtin.isEmpty();
        if (status.state == PluginStatus::State::Enabled ||
            status.state == PluginStatus::State::Disabled) {
            qCInfo(lcPlugins).noquote() << "外掛" << status.id << toString(status.state);
        } else {
            qCWarning(lcPlugins).noquote()
                << "外掛" << status.id << toString(status.state) << status.reason;
        }
        m_loaded.append(std::move(status));
    }
}

QList<PluginStatus> PluginManager::list() const {
    const QString staging = m_pluginsDir + QLatin1Char('/') + kStagingName;
    const QStringList disabled = settingsList(kDisabledKey);
    const QStringList removing = settingsList(kPendingRemoveKey);
    QList<PluginStatus> result = m_loaded;
    QStringList known;
    for (PluginStatus& status : result) {
        known.append(status.id);
        const bool wasDisabled = m_disabledAtLoad.contains(status.id);
        if (removing.contains(status.id)) {
            status.pending = PluginStatus::Pending::Remove;
        } else if (QFileInfo::exists(staging + QLatin1Char('/') + status.id)) {
            status.pending = PluginStatus::Pending::Install;
        } else if (wasDisabled != disabled.contains(status.id)) {
            status.pending =
                wasDisabled ? PluginStatus::Pending::Enable : PluginStatus::Pending::Disable;
        }
    }
    for (const QString& name : pluginFolders(staging)) {
        if (known.contains(name) || !isValidPluginId(name)) {
            continue;
        }
        PluginStatus status;
        status.id = name;
        const auto manifest = readPluginManifest(staging + QLatin1Char('/') + name +
                                                 QLatin1Char('/') + kManifestName);
        if (manifest) {
            status.name = manifest.value().name;
            status.type = manifest.value().type;
            status.version = manifest.value().version;
            status.license = manifest.value().license;
            status.url = manifest.value().url;
        }
        status.state = PluginStatus::State::NotInstalled;
        status.pending = PluginStatus::Pending::Install;
        result.append(std::move(status));
    }
    sortById(result);
    return result;
}

Result<bool> PluginManager::importPlugin(const QString& manifestPath) {
    const auto manifest = readPluginManifest(manifestPath);
    if (!manifest) {
        return manifest.error();
    }
    const PluginManifest& m = manifest.value();
    PluginStatus check;
    check.type = m.type;
    if (!checkCompatible(check, m.abi, m.appMin, m.appMax)) {
        return manageError(check.reason, "PluginManager::importPlugin");
    }
    const QString sourceDir = QFileInfo(manifestPath).absolutePath();
    const QString sourceDll = sourceDir + QLatin1Char('/') + m.dll;
    if (!QFileInfo(sourceDll).isFile()) {
        return manageError(QStringLiteral("找不到外掛的 DLL：%1").arg(m.dll),
                           "PluginManager::importPlugin");
    }
    const QByteArray expected = officialHash(m.id);
    const auto actual = sha256OfFile(sourceDll);
    if (!actual) {
        return actual.error();
    }
    if (expected.isEmpty() || actual.value().compare(expected, Qt::CaseInsensitive) != 0) {
        return manageError(QStringLiteral("不是這個版本的官方外掛（檔案不符）"),
                           "PluginManager::importPlugin");
    }
    // 先寫 <id>.tmp，複製並再驗一次雜湊後才改名成 <id>：重新啟動時只會看到完整的一份
    const QString staging = m_pluginsDir + QLatin1Char('/') + kStagingName;
    const QString temp = staging + QLatin1Char('/') + m.id + QStringLiteral(".tmp");
    const QString target = staging + QLatin1Char('/') + m.id;
    QDir(temp).removeRecursively();
    if (!QDir().mkpath(temp) ||
        !QFile::copy(manifestPath, temp + QLatin1Char('/') + kManifestName) ||
        !QFile::copy(sourceDll, temp + QLatin1Char('/') + m.dll)) {
        QDir(temp).removeRecursively();
        return manageError(
            QStringLiteral("無法複製外掛到 %1").arg(QDir::toNativeSeparators(staging)),
            "PluginManager::importPlugin");
    }
    const auto copied = sha256OfFile(temp + QLatin1Char('/') + m.dll);
    if (!copied || copied.value().compare(expected, Qt::CaseInsensitive) != 0) {
        QDir(temp).removeRecursively();
        return manageError(QStringLiteral("複製後的外掛檔案不符，請再匯入一次"),
                           "PluginManager::importPlugin");
    }
    QDir(target).removeRecursively();
    if (!QDir().rename(temp, target)) {
        QDir(temp).removeRecursively();
        return manageError(QStringLiteral("無法安裝外掛（改名失敗）"),
                           "PluginManager::importPlugin");
    }
    QStringList removing = settingsList(kPendingRemoveKey);
    removing.removeAll(m.id);
    setSettingsList(kPendingRemoveKey, removing);
    qCInfo(lcPlugins).noquote() << "已匯入外掛，重新啟動後安裝：" << m.id << m.version;
    return true;
}

Result<bool> PluginManager::remove(const QString& id) {
    const QString staged = m_pluginsDir + QLatin1Char('/') + kStagingName + QLatin1Char('/') + id;
    const QString installed = m_pluginsDir + QLatin1Char('/') + id;
    if (!isValidPluginId(id) || (!QFileInfo::exists(staged) && !QFileInfo::exists(installed))) {
        return manageError(builtinFolder(id).isEmpty()
                               ? QStringLiteral("沒有安裝外掛 %1").arg(id)
                               : QStringLiteral("內建的外掛不能移除，可以停用"),
                           "PluginManager::remove");
    }
    QDir(staged).removeRecursively();
    if (QFileInfo::exists(installed)) {
        QStringList removing = settingsList(kPendingRemoveKey);
        if (!removing.contains(id)) {
            removing.append(id);
        }
        setSettingsList(kPendingRemoveKey, removing);
    }
    return true;
}

Result<bool> PluginManager::setEnabled(const QString& id, bool enabled) {
    const QString staged = m_pluginsDir + QLatin1Char('/') + kStagingName + QLatin1Char('/') + id;
    if (!isValidPluginId(id) ||
        (!QFileInfo::exists(staged) && !QFileInfo::exists(m_pluginsDir + QLatin1Char('/') + id) &&
         builtinFolder(id).isEmpty())) {
        return manageError(QStringLiteral("沒有安裝外掛 %1").arg(id), "PluginManager::setEnabled");
    }
    QStringList disabled = settingsList(kDisabledKey);
    disabled.removeAll(id);
    if (!enabled) {
        disabled.append(id);
    }
    setSettingsList(kDisabledKey, disabled);
    return true;
}

} // namespace auralis::core
