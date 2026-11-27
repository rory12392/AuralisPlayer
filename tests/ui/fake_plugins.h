#pragma once

// 設定頁「外掛」分區的 Qt Quick Test（tst_plugins，T-104）：Plugins 單例背後是假的外掛管理。
// QML 以 pluginsProbe 設定清單的狀態、讓下一個操作失敗、讀回呼叫紀錄與開過的網址。
// 匯入成功時加一列「重新啟動後安裝」，移除、停用、啟用只記下待辦
// （和 PluginManager 一樣重新啟動後生效）。

#include "shell/plugins_controller.h"
#include "ui_scale_probe.h"

#include <auralis/dialogs.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace auralis::test {

class PluginsProbe : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList calls READ calls NOTIFY changed)
    Q_PROPERTY(QStringList openedUrls READ openedUrls NOTIFY changed)

public:
    using Status = core::PluginStatus;

    PluginsProbe() {
        ui::PluginsController::setOpenUrlHook([this](const QUrl& url) {
            m_openedUrls.append(url.toString());
            emit changed();
            return true;
        });
    }
    ~PluginsProbe() override { ui::PluginsController::setOpenUrlHook({}); }

    /// 主程式的接法：在建立 QML 引擎後、測試載入前呼叫一次。
    /// 設定頁另一個要假資料的分區（外觀的介面縮放，tst_ui_scale，T-116）也在這裡接好：
    /// quick_test_main.cpp 已到單檔 400 行上限，放不下另外的呼叫。
    void install(QQmlEngine* engine, Dialogs* dialogs) {
        m_controller = std::make_unique<ui::PluginsController>(backend(), dialogs);
        ui::PluginsController::setInstance(m_controller.get());
        engine->rootContext()->setContextProperty(QStringLiteral("pluginsProbe"), this);
        UiScaleProbe::install(engine);
    }

    QStringList calls() const { return m_calls; }
    QStringList openedUrls() const { return m_openedUrls; }

    /// 清單：[{id, name, type, version, state, reason, pending, builtin, logoDark, logoLight}]
    /// （state、pending 用 toString 的寫法；logo 是本機路徑，T-105）。
    Q_INVOKABLE void setPlugins(const QVariantList& rows) {
        m_plugins.clear();
        for (const auto& value : rows) {
            const QVariantMap row = value.toMap();
            Status status;
            status.id = row.value(QStringLiteral("id")).toString();
            status.name = row.value(QStringLiteral("name")).toString();
            status.type = row.value(QStringLiteral("type"), QStringLiteral("output")).toString();
            status.version = row.value(QStringLiteral("version")).toString();
            status.reason = row.value(QStringLiteral("reason")).toString();
            status.state = state(row.value(QStringLiteral("state")).toString());
            status.pending = pending(row.value(QStringLiteral("pending")).toString());
            status.builtin = row.value(QStringLiteral("builtin")).toBool();
            status.logoDark = row.value(QStringLiteral("logoDark")).toString();
            status.logoLight = row.value(QStringLiteral("logoLight")).toString();
            m_plugins.append(status);
        }
        emit m_controller->changed();
    }
    /// repo 裡的 ASIO Compatible 標誌（theme 是 dark 或 light）的本機路徑（T-105）。
    Q_INVOKABLE QString repoLogo(const QString& theme) const {
        return QDir::cleanPath(QStringLiteral(QUICK_TEST_SOURCE_DIR
                                              "/../../../src/plugins/asio/logo/"
                                              "asio-compatible-logo-%1.svg")
                                   .arg(theme));
    }
    /// 下一個操作（匯入、移除、停用）回傳這個錯誤。
    Q_INVOKABLE void failNext(const QString& message) { m_nextError = message; }
    Q_INVOKABLE void reset() {
        m_plugins.clear();
        m_calls.clear();
        m_openedUrls.clear();
        m_nextError.clear();
        emit changed();
        emit m_controller->changed();
    }
    /// 放一個解開的外掛資料夾（plugin.json＋DLL，內容不重要），回傳資料夾路徑（/ 分隔）。
    Q_INVOKABLE QString makePluginFolder(const QString& name, bool withManifest) const {
        const QString dir =
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-104-quick/") + name;
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        write(dir + QStringLiteral("/AuralisAsio.dll"), "MZ");
        if (withManifest) {
            write(dir + QStringLiteral("/plugin.json"), "{}");
        }
        return dir;
    }

signals:
    void changed();

private:
    static void write(const QString& path, const QByteArray& bytes) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
        }
    }
    static Status::State state(const QString& text) {
        for (const auto value :
             {Status::State::Enabled, Status::State::Disabled, Status::State::VersionMismatch,
              Status::State::FileMismatch, Status::State::UnsupportedType, Status::State::Invalid,
              Status::State::LoadFailed, Status::State::NotInstalled}) {
            if (core::toString(value) == text) {
                return value;
            }
        }
        return Status::State::Invalid;
    }
    static Status::Pending pending(const QString& text) {
        for (const auto value : {Status::Pending::Install, Status::Pending::Remove,
                                 Status::Pending::Enable, Status::Pending::Disable}) {
            if (core::toString(value) == text) {
                return value;
            }
        }
        return Status::Pending::None;
    }
    /// 有設 failNext 時回錯一次。
    bool fail(Result<bool>& result) {
        if (m_nextError.isEmpty()) {
            return false;
        }
        result = Error{m_nextError};
        m_nextError.clear();
        return true;
    }
    void setPending(const QString& id, Status::Pending value) {
        for (auto& status : m_plugins) {
            if (status.id == id) {
                status.pending = value;
            }
        }
    }
    void log(const QString& call) {
        m_calls.append(call);
        emit changed();
    }

    ui::PluginsBackend backend() {
        return {[this] { return m_plugins; },
                [this](const QString& path) {
                    log(QStringLiteral("import:") + QDir::fromNativeSeparators(path));
                    Result<bool> result = true;
                    if (!fail(result)) {
                        Status status;
                        status.id = QStringLiteral("AuralisAsio");
                        status.name = QStringLiteral("ASIO 輸出");
                        status.type = QStringLiteral("output");
                        status.version = QStringLiteral("0.1.0");
                        status.state = Status::State::NotInstalled;
                        status.pending = Status::Pending::Install;
                        m_plugins.append(status);
                    }
                    return result;
                },
                [this](const QString& id) {
                    log(QStringLiteral("remove:") + id);
                    Result<bool> result = true;
                    if (!fail(result)) {
                        setPending(id, Status::Pending::Remove);
                    }
                    return result;
                },
                [this](const QString& id, bool enabled) {
                    log(QStringLiteral("enable:%1:%2")
                            .arg(id, enabled ? QStringLiteral("true") : QStringLiteral("false")));
                    Result<bool> result = true;
                    if (!fail(result)) {
                        setPending(id,
                                   enabled ? Status::Pending::Enable : Status::Pending::Disable);
                    }
                    return result;
                }};
    }

    QList<Status> m_plugins;
    QStringList m_calls;
    QStringList m_openedUrls;
    QString m_nextError;
    std::unique_ptr<ui::PluginsController> m_controller;
};

} // namespace auralis::test
