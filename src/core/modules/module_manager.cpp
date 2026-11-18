#include "modules/module_manager.h"

#include "library/module_database_impl.h"
#include "settings/module_settings_impl.h"

#include <QLoggingCategory>
#include <QSettings>

#include <exception>

Q_LOGGING_CATEGORY(lcModules, "auralis.modules")

namespace auralis::core {

namespace {

/// 每個模組一份入口；database()、settings() 只屬於這個模組。
class ModuleHostImpl final : public ModuleHost {
public:
    ModuleHostImpl(CoreServices& services, QString code, const QString& settingsPath)
        : m_services(services), m_code(std::move(code)), m_database(m_code, services.databasePath),
          m_settings(settingsPath, m_code) {}

    QString moduleCode() const override { return m_code; }
    JobRunner& jobs() override { return m_services.jobs; }
    LibraryQuery& library() override { return m_services.library; }
    ModuleDatabase& database() override { return m_database; }
    ModuleSettings& settings() override { return m_settings; }
    PlaybackService& playback() override { return m_services.playback; }
    OutputRegistry& outputs() override { return m_services.outputs; }
    SourceRegistry& sources() override { return m_services.sources; }
    ProcessorChain& processors() override { return m_services.processors; }
    CoverService& covers() override { return m_services.covers; }
    UiRegistry& ui() override { return m_services.ui; }
    Navigator& navigator() override { return m_services.navigator; }
    Dialogs& dialogs() override { return m_services.dialogs; }
    Notifier& notifier() override { return m_services.notifier; }
    PlatformWindow& platformWindow() override { return m_services.platformWindow; }
    PluginRegistry& plugins() override { return m_services.plugins; }

private:
    CoreServices& m_services;
    QString m_code;
    ModuleDatabaseImpl m_database;
    ModuleSettingsImpl m_settings;
};

QString describe(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("未知的例外");
    }
}

} // namespace

struct ModuleManager::Entry {
    std::unique_ptr<ModuleHostImpl> host;
    std::unique_ptr<IModule> module;
};

ModuleManager::ModuleManager(CoreServices& services, QString settingsPath,
                             std::vector<ModuleRegistration> modules)
    : m_services(services), m_settingsPath(std::move(settingsPath)),
      m_registrations(std::move(modules)) {}

ModuleManager::~ModuleManager() {
    stopAll();
}

void ModuleManager::startAll() {
    const QSettings settings(m_settingsPath, QSettings::IniFormat);
    for (const auto& registration : m_registrations) {
        ModuleStatus status;
        status.code = registration.code;

        const QString key = QStringLiteral("modules/%1/enabled").arg(registration.code);
        if (!settings.value(key, true).toBool()) {
            status.state = ModuleStatus::State::Disabled;
            qCInfo(lcModules).noquote() << "模組已停用：" << registration.code;
            m_statuses.push_back(std::move(status));
            continue;
        }

        // 錯誤邊界：建立、讀資訊、start() 任何一步丟出例外，都只讓這個模組失敗
        auto entry = std::make_unique<Entry>();
        try {
            entry->module = registration.factory();
            const ModuleInfo info = entry->module->info();
            status.name = info.name;
            status.version = info.version;
            entry->host =
                std::make_unique<ModuleHostImpl>(m_services, registration.code, m_settingsPath);
            entry->module->start(*entry->host);
            status.state = ModuleStatus::State::Started;
            m_started.push_back(std::move(entry));
            qCInfo(lcModules).noquote() << "模組已啟動：" << registration.code;
        } catch (...) {
            status.state = ModuleStatus::State::Failed;
            status.error = describe(std::current_exception());
            qCCritical(lcModules).noquote()
                << "模組啟動失敗：" << registration.code << status.error;
        }
        m_statuses.push_back(std::move(status));
    }
}

void ModuleManager::stopAll() noexcept {
    while (!m_started.empty()) {
        m_started.back()->module->stop();
        m_started.pop_back();
    }
}

QString toString(ModuleStatus::State state) {
    switch (state) {
    case ModuleStatus::State::Started:
        return QStringLiteral("started");
    case ModuleStatus::State::Disabled:
        return QStringLiteral("disabled");
    case ModuleStatus::State::Failed:
        return QStringLiteral("failed");
    }
    return {};
}

} // namespace auralis::core
