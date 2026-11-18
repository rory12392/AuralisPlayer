// 外掛管理測試的共用工具：測試資料夾、假外掛的安裝與官方雜湊（T-103）。
#pragma once

#include "plugins/plugin_manager.h"

#include <auralis/plugin_entry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace auralis::test {

/// 假外掛 DLL（tests/core/plugins/fake_plugin.cpp 建出來的）的路徑：mode 是 ok、throw、null、abi。
inline QString fakePluginDll(const QString& mode) {
    return QStringLiteral(AURALIS_FAKE_PLUGINS "/fake_plugin_%1.dll").arg(mode);
}

inline QByteArray sha256Hex(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

/// 一個測試用的資料夾（每次開始先清掉上一輪的殘留）。
struct PluginFixture {
    QString dataDir;
    QString settingsPath;
    QString pluginsDir;
    /// 模擬程式資料夾的 plugins\（內建外掛，T-105）。
    QString builtinDir;
    std::vector<core::OfficialPlugin> official;

    explicit PluginFixture(const QString& name) {
        dataDir = QStringLiteral(AURALIS_TEST_TMP "/plugins/%1").arg(name);
        QDir(dataDir).removeRecursively();
        QDir().mkpath(dataDir);
        settingsPath = dataDir + QStringLiteral("/settings.ini");
        pluginsDir = dataDir + QStringLiteral("/plugins");
        builtinDir = dataDir + QStringLiteral("/app/plugins");
    }

    /// 正確的 plugin.json 內容（output 類型、這一版的 ABI、主程式版本 0.1.0）。
    static QJsonObject manifest(const QString& id, const QString& dll) {
        return {{QStringLiteral("id"), id},
                {QStringLiteral("name"), QStringLiteral("測試外掛")},
                {QStringLiteral("type"), QStringLiteral("output")},
                {QStringLiteral("version"), QStringLiteral("0.1.0")},
                {QStringLiteral("appVersion"),
                 QJsonObject{{QStringLiteral("min"), QStringLiteral("0.1.0")},
                             {QStringLiteral("max"), QStringLiteral("0.1.0")}}},
                {QStringLiteral("abi"), static_cast<int>(AURALIS_PLUGIN_ABI_VERSION)},
                {QStringLiteral("dll"), dll},
                {QStringLiteral("license"), QStringLiteral("MIT")},
                {QStringLiteral("url"), QStringLiteral("https://example.invalid/plugin")}};
    }

    /// 在 folder 放 plugin.json（原文 json）與 mode 的假 DLL（檔名 dll）；回傳 plugin.json 的路徑。
    static QString writeFolder(const QString& folder, const QByteArray& json, const QString& mode,
                               const QString& dll) {
        QDir().mkpath(folder);
        const QString manifestPath = folder + QStringLiteral("/plugin.json");
        QFile file(manifestPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(json);
        }
        if (!mode.isEmpty()) {
            QFile::remove(folder + QLatin1Char('/') + dll);
            QFile::copy(fakePluginDll(mode), folder + QLatin1Char('/') + dll);
        }
        return manifestPath;
    }

    /// 直接放進 plugins\<id>\（模擬已安裝）；official 為 true 時把這個 DLL 的雜湊當成官方的。
    QString install(const QString& id, const QString& mode, const QJsonObject& json,
                    bool officialHash = true) {
        const QString dll = json.value(QStringLiteral("dll")).toString();
        const QString folder = pluginsDir + QLatin1Char('/') + id;
        const QString path =
            writeFolder(folder, QJsonDocument(json).toJson(), mode, dll.isEmpty() ? id : dll);
        if (officialHash && !mode.isEmpty()) {
            official.push_back({id, sha256Hex(fakePluginDll(mode))});
        }
        return path;
    }

    /// 放進程式資料夾的 plugins\<id>\（內建外掛）；這個 DLL 的雜湊當成官方的。
    QString installBuiltin(const QString& id, const QString& mode, const QJsonObject& json) {
        const QString dll = json.value(QStringLiteral("dll")).toString();
        const QString path = writeFolder(builtinDir + QLatin1Char('/') + id,
                                         QJsonDocument(json).toJson(), mode, dll);
        official.push_back({id, sha256Hex(fakePluginDll(mode))});
        return path;
    }

    core::PluginManager manager() const {
        return core::PluginManager(dataDir, settingsPath, QStringLiteral("0.1.0"), official,
                                   builtinDir);
    }
};

/// 狀態清單裡找 id；沒有時回傳 nullptr。
inline const core::PluginStatus* findStatus(const QList<core::PluginStatus>& list,
                                            const QString& id) {
    for (const auto& status : list) {
        if (status.id == id) {
            return &status;
        }
    }
    return nullptr;
}

} // namespace auralis::test
