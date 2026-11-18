// 外掛管理：匯入、移除、停用都在重新啟動後生效（T-103，AC-03-0）。
// 「重新啟動」在測試裡是同一個資料夾建一個新的 PluginManager 再 loadAll。
#include "plugin_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QSettings>

using auralis::PluginType;
using auralis::core::PluginRegistryImpl;
using auralis::core::PluginStatus;
using namespace auralis::test;

namespace {

/// 模擬重新啟動：新的管理器載入一次，回傳它（狀態清單與 registry 由呼叫端看）。
auralis::core::PluginManager restart(const PluginFixture& fixture, PluginRegistryImpl& registry) {
    auto manager = fixture.manager();
    manager.loadAll(registry);
    return manager;
}

/// 在資料夾外的「下載位置」放一份外掛（plugin.json＋DLL），回傳 plugin.json 的路徑。
QString downloaded(const PluginFixture& fixture, const QString& id, const QString& mode,
                   const QString& dll) {
    return PluginFixture::writeFolder(fixture.dataDir + QStringLiteral("/download/") + id,
                                      QJsonDocument(PluginFixture::manifest(id, dll)).toJson(),
                                      mode, dll);
}

} // namespace

TEST_CASE("AC-03-0：匯入官方外掛回傳需要重新啟動，重新啟動後已啟用") {
    PluginFixture fixture(QStringLiteral("manage-import"));
    fixture.official.push_back(
        {QStringLiteral("fake-import"), sha256Hex(fakePluginDll(QStringLiteral("ok")))});
    const QString source = downloaded(fixture, QStringLiteral("fake-import"), QStringLiteral("ok"),
                                      QStringLiteral("import.dll"));
    {
        auto manager = fixture.manager();
        PluginRegistryImpl registry;
        manager.loadAll(registry);
        const auto result = manager.importPlugin(source);
        REQUIRE(result.hasValue());
        CHECK(result.value()); // 需要重新啟動
        // 這次執行不載入：清單列出「重新啟動後安裝」
        CHECK(registry.loaded(PluginType::Output).isEmpty());
        const auto list = manager.list();
        const PluginStatus* status = findStatus(list, QStringLiteral("fake-import"));
        REQUIRE(status != nullptr);
        CHECK(status->state == PluginStatus::State::NotInstalled);
        CHECK(status->pending == PluginStatus::Pending::Install);
    }
    PluginRegistryImpl registry;
    const auto manager = restart(fixture, registry);
    const auto list = manager.list();
    const PluginStatus* status = findStatus(list, QStringLiteral("fake-import"));
    REQUIRE(status != nullptr);
    CHECK(status->state == PluginStatus::State::Enabled);
    CHECK(status->pending == PluginStatus::Pending::None);
    CHECK(registry.loaded(PluginType::Output).size() == 1);
    CHECK(QFileInfo::exists(fixture.pluginsDir + QStringLiteral("/fake-import/import.dll")));
    CHECK(QFileInfo::exists(fixture.pluginsDir + QStringLiteral("/fake-import/plugin.json")));
}

TEST_CASE("AC-03-0：匯入不是官方的外掛時拒絕，什麼都不寫") {
    PluginFixture fixture(QStringLiteral("manage-import-bad"));
    fixture.official.push_back({QStringLiteral("fake-evil"), QByteArray(64, '0')});
    const QString source = downloaded(fixture, QStringLiteral("fake-evil"), QStringLiteral("ok"),
                                      QStringLiteral("evil.dll"));
    auto manager = fixture.manager();
    const auto result = manager.importPlugin(source);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("不是這個版本的官方外掛")));
    CHECK_FALSE(QFileInfo::exists(fixture.pluginsDir));
    CHECK(manager.list().isEmpty());
}

TEST_CASE("AC-03-0：匯入格式錯、不支援類型或找不到 DLL 的外掛時拒絕") {
    PluginFixture fixture(QStringLiteral("manage-import-invalid"));
    fixture.official.push_back(
        {QStringLiteral("fake-x"), sha256Hex(fakePluginDll(QStringLiteral("ok")))});
    QJsonObject json = PluginFixture::manifest(QStringLiteral("fake-x"), QStringLiteral("x.dll"));
    QString mode = QStringLiteral("ok");
    SECTION("不支援的類型") {
        json.insert(QStringLiteral("type"), QStringLiteral("decoder"));
    }
    SECTION("缺欄位") {
        json.remove(QStringLiteral("license"));
    }
    SECTION("找不到 DLL") {
        mode.clear();
    }
    const QString source =
        PluginFixture::writeFolder(fixture.dataDir + QStringLiteral("/dl/x"),
                                   QJsonDocument(json).toJson(), mode, QStringLiteral("x.dll"));
    auto manager = fixture.manager();
    const auto result = manager.importPlugin(source);
    REQUIRE_FALSE(result.hasValue());
    CHECK_FALSE(result.error().message.isEmpty());
    CHECK_FALSE(QFileInfo::exists(fixture.pluginsDir + QStringLiteral("/fake-x")));
}

TEST_CASE("AC-03-0：移除回傳需要重新啟動，重新啟動後外掛資料夾不在、清單沒有它") {
    PluginFixture fixture(QStringLiteral("manage-remove"));
    fixture.install(QStringLiteral("fake-rm"), QStringLiteral("ok"),
                    PluginFixture::manifest(QStringLiteral("fake-rm"), QStringLiteral("rm.dll")));
    {
        // 先停用，這次執行不載入 DLL：同一個測試行程裡載入過的 DLL
        // 刪不掉（真實程式是下一個行程才刪）
        QSettings settings(fixture.settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("plugins/disabled"),
                          QStringList{QStringLiteral("fake-rm")});
    }
    {
        auto manager = fixture.manager();
        PluginRegistryImpl registry;
        manager.loadAll(registry);
        const auto result = manager.remove(QStringLiteral("fake-rm"));
        REQUIRE(result.hasValue());
        CHECK(result.value());
        CHECK(QFileInfo::exists(fixture.pluginsDir + QStringLiteral("/fake-rm/rm.dll")));
        const auto list = manager.list();
        const PluginStatus* status = findStatus(list, QStringLiteral("fake-rm"));
        REQUIRE(status != nullptr);
        CHECK(status->pending == PluginStatus::Pending::Remove);
    }
    PluginRegistryImpl registry;
    const auto manager = restart(fixture, registry);
    CHECK(manager.list().isEmpty());
    CHECK(registry.loaded(PluginType::Output).isEmpty());
    CHECK_FALSE(QFileInfo::exists(fixture.pluginsDir + QStringLiteral("/fake-rm")));
}

TEST_CASE("AC-03-0：移除沒有安裝的外掛時回錯") {
    PluginFixture fixture(QStringLiteral("manage-remove-none"));
    auto manager = fixture.manager();
    CHECK_FALSE(manager.remove(QStringLiteral("nothing")).hasValue());
    CHECK_FALSE(manager.remove(QStringLiteral("..")).hasValue());
}

TEST_CASE("AC-03-0：停用與啟用回傳需要重新啟動，重新啟動後才生效") {
    PluginFixture fixture(QStringLiteral("manage-enable"));
    fixture.install(QStringLiteral("fake-sw"), QStringLiteral("ok"),
                    PluginFixture::manifest(QStringLiteral("fake-sw"), QStringLiteral("sw.dll")));
    {
        auto manager = fixture.manager();
        PluginRegistryImpl first;
        manager.loadAll(first);
        CHECK(first.loaded(PluginType::Output).size() == 1);
        const auto result = manager.setEnabled(QStringLiteral("fake-sw"), false);
        REQUIRE(result.hasValue());
        CHECK(result.value());
        const auto list = manager.list();
        const PluginStatus* status = findStatus(list, QStringLiteral("fake-sw"));
        REQUIRE(status != nullptr);
        CHECK(status->pending == PluginStatus::Pending::Disable);
    }
    {
        PluginRegistryImpl registry;
        auto manager = restart(fixture, registry);
        const auto list = manager.list();
        const PluginStatus* status = findStatus(list, QStringLiteral("fake-sw"));
        REQUIRE(status != nullptr);
        CHECK(status->state == PluginStatus::State::Disabled);
        CHECK(status->pending == PluginStatus::Pending::None);
        CHECK(registry.loaded(PluginType::Output).isEmpty());
        const auto result = manager.setEnabled(QStringLiteral("fake-sw"), true);
        REQUIRE(result.hasValue());
        CHECK(result.value());
        const auto after = manager.list();
        CHECK(findStatus(after, QStringLiteral("fake-sw"))->pending ==
              PluginStatus::Pending::Enable);
    }
    PluginRegistryImpl registry;
    const auto manager = restart(fixture, registry);
    const auto list = manager.list();
    CHECK(findStatus(list, QStringLiteral("fake-sw"))->state == PluginStatus::State::Enabled);
    CHECK(registry.loaded(PluginType::Output).size() == 1);
}

TEST_CASE("AC-03-0：停用沒有安裝的外掛時回錯") {
    PluginFixture fixture(QStringLiteral("manage-enable-none"));
    auto manager = fixture.manager();
    CHECK_FALSE(manager.setEnabled(QStringLiteral("nothing"), false).hasValue());
}
