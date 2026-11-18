// 外掛管理：程式資料夾的內建外掛（T-105，AC-03-0 v1.8）與外掛的標誌欄位。
// 內建外掛放在 <程式資料夾>\plugins\<id>\，預設啟用、可以停用、不能移除；
// 使用者匯入的同 id 外掛能用就用它，不能用（版本或檔案不符）時改用內建的。
#include "plugin_fixtures.h"
#include "plugins/plugin_manifest.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

using auralis::PluginType;
using auralis::core::PluginRegistryImpl;
using auralis::core::PluginStatus;
using namespace auralis::test;

namespace {

/// 模擬一次啟動，回傳 id 的狀態（找不到時測試失敗）。
PluginStatus start(const PluginFixture& fixture, const QString& id, PluginRegistryImpl& registry) {
    auto manager = fixture.manager();
    manager.loadAll(registry);
    const auto list = manager.list();
    const PluginStatus* status = findStatus(list, id);
    REQUIRE(status != nullptr);
    return *status;
}

QString clean(const QString& path) {
    return QDir::cleanPath(path);
}

} // namespace

TEST_CASE("AC-03-0：程式資料夾的內建外掛被掃到、預設啟用，標成內建") {
    PluginFixture fixture(QStringLiteral("builtin-ok"));
    fixture.installBuiltin(
        QStringLiteral("fake-bi"), QStringLiteral("ok"),
        PluginFixture::manifest(QStringLiteral("fake-bi"), QStringLiteral("bi.dll")));
    PluginRegistryImpl registry;
    const PluginStatus status = start(fixture, QStringLiteral("fake-bi"), registry);
    CHECK(status.state == PluginStatus::State::Enabled);
    CHECK(status.builtin);
    CHECK(clean(status.folder) == clean(fixture.builtinDir + QStringLiteral("/fake-bi")));
    CHECK(registry.loaded(PluginType::Output).size() == 1);
    // 使用者的資料夾沒有被寫入任何東西
    CHECK_FALSE(QFileInfo::exists(fixture.pluginsDir));
}

TEST_CASE("AC-03-0：程式資料夾裡不是官方外掛的資料夾（Qt 的外掛等）不列出") {
    PluginFixture fixture(QStringLiteral("builtin-ignore"));
    QDir().mkpath(fixture.builtinDir + QStringLiteral("/platforms"));
    PluginFixture::writeFolder(
        fixture.builtinDir + QStringLiteral("/fake-stranger"),
        QJsonDocument(
            PluginFixture::manifest(QStringLiteral("fake-stranger"), QStringLiteral("s.dll")))
            .toJson(),
        QStringLiteral("ok"), QStringLiteral("s.dll"));
    auto manager = fixture.manager();
    PluginRegistryImpl registry;
    manager.loadAll(registry);
    CHECK(manager.list().isEmpty());
    CHECK(registry.loaded(PluginType::Output).isEmpty());
}

TEST_CASE("AC-03-0：內建外掛可以停用，重新啟動後不載入；再啟用後載入") {
    PluginFixture fixture(QStringLiteral("builtin-disable"));
    fixture.installBuiltin(
        QStringLiteral("fake-bd"), QStringLiteral("ok"),
        PluginFixture::manifest(QStringLiteral("fake-bd"), QStringLiteral("bd.dll")));
    {
        auto manager = fixture.manager();
        PluginRegistryImpl registry;
        manager.loadAll(registry);
        const auto result = manager.setEnabled(QStringLiteral("fake-bd"), false);
        REQUIRE(result.hasValue());
        CHECK(result.value());
        CHECK(findStatus(manager.list(), QStringLiteral("fake-bd"))->pending ==
              PluginStatus::Pending::Disable);
    }
    {
        PluginRegistryImpl registry;
        const PluginStatus status = start(fixture, QStringLiteral("fake-bd"), registry);
        CHECK(status.state == PluginStatus::State::Disabled);
        CHECK(status.builtin);
        CHECK(registry.loaded(PluginType::Output).isEmpty());
        auto manager = fixture.manager();
        REQUIRE(manager.setEnabled(QStringLiteral("fake-bd"), true).hasValue());
    }
    PluginRegistryImpl registry;
    CHECK(start(fixture, QStringLiteral("fake-bd"), registry).state ==
          PluginStatus::State::Enabled);
    CHECK(registry.loaded(PluginType::Output).size() == 1);
}

TEST_CASE("AC-03-0：內建外掛不能移除（回錯、程式資料夾不動）") {
    PluginFixture fixture(QStringLiteral("builtin-remove"));
    fixture.installBuiltin(
        QStringLiteral("fake-br"), QStringLiteral("ok"),
        PluginFixture::manifest(QStringLiteral("fake-br"), QStringLiteral("br.dll")));
    auto manager = fixture.manager();
    PluginRegistryImpl registry;
    manager.loadAll(registry);
    const auto result = manager.remove(QStringLiteral("fake-br"));
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.contains(QStringLiteral("內建")));
    CHECK(findStatus(manager.list(), QStringLiteral("fake-br"))->pending ==
          PluginStatus::Pending::None);
    CHECK(QFileInfo::exists(fixture.builtinDir + QStringLiteral("/fake-br/br.dll")));
}

TEST_CASE("AC-03-0：使用者匯入的同 id 外掛能用時用它（仍標成內建，移除只刪使用者那份）") {
    PluginFixture fixture(QStringLiteral("builtin-user-ok"));
    const auto json = PluginFixture::manifest(QStringLiteral("fake-bu"), QStringLiteral("bu.dll"));
    fixture.installBuiltin(QStringLiteral("fake-bu"), QStringLiteral("ok"), json);
    fixture.install(QStringLiteral("fake-bu"), QStringLiteral("ok"), json);
    PluginRegistryImpl registry;
    const PluginStatus status = start(fixture, QStringLiteral("fake-bu"), registry);
    CHECK(status.state == PluginStatus::State::Enabled);
    CHECK(status.builtin);
    CHECK(clean(status.folder) == clean(fixture.pluginsDir + QStringLiteral("/fake-bu")));
    CHECK(registry.loaded(PluginType::Output).size() == 1);
}

TEST_CASE("AC-03-0：使用者匯入的同 id 外掛版本或檔案不符時不採用，改用內建的") {
    PluginFixture fixture(QStringLiteral("builtin-user-bad"));
    const auto json = PluginFixture::manifest(QStringLiteral("fake-bx"), QStringLiteral("bx.dll"));
    fixture.installBuiltin(QStringLiteral("fake-bx"), QStringLiteral("ok"), json);
    SECTION("版本不符") {
        QJsonObject old = json;
        old.insert(QStringLiteral("appVersion"),
                   QJsonObject{{QStringLiteral("min"), QStringLiteral("0.0.9")},
                               {QStringLiteral("max"), QStringLiteral("0.0.9")}});
        fixture.install(QStringLiteral("fake-bx"), QStringLiteral("ok"), old, false);
    }
    SECTION("檔案不符") {
        fixture.install(QStringLiteral("fake-bx"), QStringLiteral("null"), json, false);
    }
    PluginRegistryImpl registry;
    const PluginStatus status = start(fixture, QStringLiteral("fake-bx"), registry);
    CHECK(status.state == PluginStatus::State::Enabled);
    CHECK(status.builtin);
    CHECK(clean(status.folder) == clean(fixture.builtinDir + QStringLiteral("/fake-bx")));
    CHECK(registry.loaded(PluginType::Output).size() == 1);
}

TEST_CASE("AC-03-0：plugin.json 的 logo 欄位給深淺兩版標誌的檔案；檔案不在時是空的") {
    PluginFixture fixture(QStringLiteral("builtin-logo"));
    QJsonObject json =
        PluginFixture::manifest(QStringLiteral("fake-logo"), QStringLiteral("lg.dll"));
    json.insert(QStringLiteral("logo"),
                QJsonObject{{QStringLiteral("dark"), QStringLiteral("logo-dark.svg")},
                            {QStringLiteral("light"), QStringLiteral("logo-light.svg")}});
    fixture.installBuiltin(QStringLiteral("fake-logo"), QStringLiteral("ok"), json);
    QFile dark(fixture.builtinDir + QStringLiteral("/fake-logo/logo-dark.svg"));
    REQUIRE(dark.open(QIODevice::WriteOnly));
    dark.write("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
    dark.close();
    PluginRegistryImpl registry;
    const PluginStatus status = start(fixture, QStringLiteral("fake-logo"), registry);
    CHECK(status.state == PluginStatus::State::Enabled);
    CHECK(clean(status.logoDark) ==
          clean(fixture.builtinDir + QStringLiteral("/fake-logo/logo-dark.svg")));
    CHECK(status.logoLight.isEmpty()); // 檔案不在
}

TEST_CASE("AC-03-0：plugin.json 的 logo 只能是外掛資料夾裡的 svg 或 png 檔名") {
    PluginFixture fixture(QStringLiteral("builtin-logo-bad"));
    for (const char* name : {"../evil.svg", "C:/x/logo.svg", "logo.exe", "sub/logo.svg"}) {
        QJsonObject json =
            PluginFixture::manifest(QStringLiteral("fake-lb"), QStringLiteral("lb.dll"));
        json.insert(QStringLiteral("logo"),
                    QJsonObject{{QStringLiteral("dark"), QString::fromLatin1(name)},
                                {QStringLiteral("light"), QStringLiteral("logo-light.svg")}});
        const QString path = fixture.install(QStringLiteral("fake-lb"), QString(), json, false);
        const auto manifest = auralis::core::readPluginManifest(path);
        INFO(name);
        REQUIRE_FALSE(manifest.hasValue());
        CHECK(manifest.error().message.contains(QStringLiteral("logo")));
    }
}
