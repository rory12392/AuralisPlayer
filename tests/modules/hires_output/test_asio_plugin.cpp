// ASIO 外掛經外掛管理載入後，OUT 模組註冊「ASIO」輸出路徑與原生 DSD（T-103，AC-03-0、AC-03-1）。
// 用這次建置的 AuralisAsio.dll 與 plugin.json；只在 AURALIS_WITH_ASIO=ON 時建置。
#include "dsd_fixtures.h"
#include "dsd_mode_controller.h"

#include "modules/module_manager.h"
#include "plugins/plugin_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

using auralis::PluginType;
using auralis::core::CoreServices;
using auralis::core::ModuleManager;
using auralis::core::PluginManager;
using auralis::core::PluginStatus;
using namespace auralis::test;

namespace {

QByteArray fileSha256(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

/// 把建好的外掛（DLL＋plugin.json）放進 <dir>\plugins\AuralisAsio\。
void installBuiltPlugin(const QString& dir) {
    const QFileInfo dll(QStringLiteral(AURALIS_ASIO_PLUGIN_DLL));
    const QString target = dir + QStringLiteral("/plugins/AuralisAsio");
    REQUIRE(QDir().mkpath(target));
    REQUIRE(QFile::copy(dll.absoluteFilePath(), target + QStringLiteral("/AuralisAsio.dll")));
    REQUIRE(QFile::copy(dll.absolutePath() + QStringLiteral("/plugin.json"),
                        target + QStringLiteral("/plugin.json")));
}

} // namespace

TEST_CASE(
    "AC-03-0：裝了這一版的 ASIO 外掛時已啟用，OUT 註冊「ASIO」輸出路徑，原生 DSD 不再要求外掛") {
    const QString dir = freshDsdDir(QStringLiteral("asio-plugin"));
    installBuiltPlugin(dir);
    const QString ini = dir + QStringLiteral("/settings.ini");
    CoreServices services;
    PluginManager plugins(
        dir, ini, QStringLiteral(AURALIS_TEST_APP_VERSION),
        {{QStringLiteral("AuralisAsio"), fileSha256(QStringLiteral(AURALIS_ASIO_PLUGIN_DLL))}});
    plugins.loadAll(services.plugins);
    const auto list = plugins.list();
    REQUIRE(list.size() == 1);
    CHECK(list.front().state == PluginStatus::State::Enabled);
    CHECK(list.front().reason.isEmpty());
    REQUIRE(services.plugins.loaded(PluginType::Output).size() == 1);

    ModuleManager manager(services, ini);
    manager.startAll();
    const auto asio = services.outputs.find(QStringLiteral("ASIO"));
    REQUIRE(asio != nullptr);
    CHECK(asio->note() == QStringLiteral("位元完美"));
    const auto* dsd = auralis::mod::out::DsdModeController::create(nullptr, nullptr);
    REQUIRE(dsd != nullptr);
    // 預設輸出不是 ASIO：原生 DSD 的原因是「需選 ASIO」，不是「需要 ASIO 外掛」
    CHECK(dsd->options().at(1).toMap().value(QStringLiteral("reason")).toString() ==
          QStringLiteral("需選 ASIO"));
    manager.stopAll();
}

TEST_CASE("AC-03-0：ASIO 外掛的 DLL 被換過（雜湊不符）時不載入，OUT 不註冊 ASIO，WASAPI 照常") {
    const QString dir = freshDsdDir(QStringLiteral("asio-plugin-bad"));
    installBuiltPlugin(dir);
    const QString ini = dir + QStringLiteral("/settings.ini");
    CoreServices services;
    PluginManager plugins(dir, ini, QStringLiteral(AURALIS_TEST_APP_VERSION),
                          {{QStringLiteral("AuralisAsio"), QByteArray(64, 'f')}});
    plugins.loadAll(services.plugins);
    const auto list = plugins.list();
    REQUIRE(list.size() == 1);
    CHECK(list.front().state == PluginStatus::State::FileMismatch);
    ModuleManager manager(services, ini);
    manager.startAll();
    CHECK(services.outputs.find(QStringLiteral("ASIO")) == nullptr);
    CHECK(services.outputs.find(QStringLiteral("WASAPI 獨占")) != nullptr);
    manager.stopAll();
}
