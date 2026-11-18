// 外掛管理：啟動時的驗證與載入（T-103，AC-03-0）。
#include "plugin_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>

using auralis::PluginType;
using auralis::core::PluginRegistryImpl;
using auralis::core::PluginStatus;
using namespace auralis::test;

namespace {

/// 載入一次並回傳 id 的狀態（找不到時測試失敗）。
PluginStatus loadOne(const PluginFixture& fixture, const QString& id,
                     PluginRegistryImpl& registry) {
    auto manager = fixture.manager();
    manager.loadAll(registry);
    const auto list = manager.list();
    const PluginStatus* status = findStatus(list, id);
    REQUIRE(status != nullptr);
    return *status;
}

} // namespace

TEST_CASE("AC-03-0：官方外掛通過驗證後載入，註冊成 output 類型、api 指向外掛的介面") {
    PluginFixture fixture(QStringLiteral("load-ok"));
    fixture.install(QStringLiteral("fake-ok"), QStringLiteral("ok"),
                    PluginFixture::manifest(QStringLiteral("fake-ok"), QStringLiteral("ok1.dll")));
    PluginRegistryImpl registry;
    const PluginStatus status = loadOne(fixture, QStringLiteral("fake-ok"), registry);
    CHECK(status.state == PluginStatus::State::Enabled);
    CHECK(status.reason.isEmpty());
    CHECK(status.name == QStringLiteral("測試外掛"));
    CHECK(status.type == QStringLiteral("output"));
    const auto loaded = registry.loaded(PluginType::Output);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front().id == QStringLiteral("fake-ok"));
    REQUIRE(loaded.front().api != nullptr);
    CHECK(*static_cast<int*>(loaded.front().api) == 42);
    CHECK(registry.loaded(PluginType::Decoder).isEmpty());
}

TEST_CASE("AC-03-0：沒有外掛資料夾時清單是空的，不報錯") {
    PluginFixture fixture(QStringLiteral("load-none"));
    auto manager = fixture.manager();
    PluginRegistryImpl registry;
    manager.loadAll(registry);
    CHECK(manager.list().isEmpty());
    CHECK(registry.loaded(PluginType::Output).isEmpty());
}

TEST_CASE("AC-03-0：plugin.json 缺欄位或格式錯時拒絕（格式錯誤），不載入 DLL") {
    struct Row {
        const char* name;
        QByteArray json;
    };
    const QJsonObject good =
        PluginFixture::manifest(QStringLiteral("fake-bad"), QStringLiteral("bad.dll"));
    auto without = [&good](const char* key) {
        QJsonObject copy = good;
        copy.remove(QString::fromLatin1(key));
        return QJsonDocument(copy).toJson();
    };
    auto with = [&good](const char* key, const QJsonValue& value) {
        QJsonObject copy = good;
        copy.insert(QString::fromLatin1(key), value);
        return QJsonDocument(copy).toJson();
    };
    const std::vector<Row> rows{
        {"不是 JSON", QByteArrayLiteral("{ id: ")},
        {"不是物件", QByteArrayLiteral("[1, 2]")},
        {"缺 id", without("id")},
        {"缺 name", without("name")},
        {"缺 type", without("type")},
        {"缺 version", without("version")},
        {"缺 appVersion", without("appVersion")},
        {"缺 abi", without("abi")},
        {"缺 dll", without("dll")},
        {"缺 license", without("license")},
        {"缺 url", without("url")},
        {"abi 不是數字", with("abi", QStringLiteral("1"))},
        {"appVersion 少了 max", with("appVersion", QJsonObject{{"min", "0.1.0"}})},
        {"dll 帶路徑", with("dll", QStringLiteral("..\\..\\evil.dll"))},
        {"dll 不是 .dll", with("dll", QStringLiteral("bad.exe"))},
        {"id 和資料夾不同", with("id", QStringLiteral("other"))},
    };
    for (const Row& row : rows) {
        DYNAMIC_SECTION(row.name) {
            PluginFixture fixture(QStringLiteral("load-invalid"));
            PluginFixture::writeFolder(fixture.pluginsDir + QStringLiteral("/fake-bad"), row.json,
                                       QStringLiteral("ok"), QStringLiteral("bad.dll"));
            fixture.official.push_back(
                {QStringLiteral("fake-bad"), sha256Hex(fakePluginDll(QStringLiteral("ok")))});
            PluginRegistryImpl registry;
            const PluginStatus status = loadOne(fixture, QStringLiteral("fake-bad"), registry);
            CHECK(status.state == PluginStatus::State::Invalid);
            CHECK_FALSE(status.reason.isEmpty());
            CHECK(registry.loaded(PluginType::Output).isEmpty());
        }
    }
}

TEST_CASE("AC-03-0：DLL 和這個版本的官方外掛雜湊不符時不載入（檔案不符）") {
    PluginFixture fixture(QStringLiteral("load-hash"));
    SECTION("雜湊不同") {
        fixture.install(
            QStringLiteral("fake-hash"), QStringLiteral("ok"),
            PluginFixture::manifest(QStringLiteral("fake-hash"), QStringLiteral("hash.dll")),
            false);
        fixture.official.push_back({QStringLiteral("fake-hash"), QByteArray(64, '0')});
    }
    SECTION("不在官方清單") {
        fixture.install(
            QStringLiteral("fake-hash"), QStringLiteral("ok"),
            PluginFixture::manifest(QStringLiteral("fake-hash"), QStringLiteral("hash.dll")),
            false);
    }
    PluginRegistryImpl registry;
    const PluginStatus status = loadOne(fixture, QStringLiteral("fake-hash"), registry);
    CHECK(status.state == PluginStatus::State::FileMismatch);
    CHECK(status.reason.contains(QStringLiteral("不是這個版本的官方外掛")));
    CHECK(registry.loaded(PluginType::Output).isEmpty());
}

TEST_CASE("AC-03-0：ABI 或主程式版本不符時不使用（版本不符）") {
    PluginFixture fixture(QStringLiteral("load-abi"));
    QString mode = QStringLiteral("ok");
    QJsonObject json =
        PluginFixture::manifest(QStringLiteral("fake-abi"), QStringLiteral("abi.dll"));
    SECTION("進入點回傳的 ABI 版本不同") {
        mode = QStringLiteral("abi");
    }
    SECTION("plugin.json 的 abi 不同") {
        json.insert(QStringLiteral("abi"), 99);
    }
    SECTION("主程式版本比範圍新") {
        json.insert(QStringLiteral("appVersion"),
                    QJsonObject{{QStringLiteral("min"), QStringLiteral("0.0.1")},
                                {QStringLiteral("max"), QStringLiteral("0.0.9")}});
    }
    SECTION("主程式版本比範圍舊") {
        json.insert(QStringLiteral("appVersion"),
                    QJsonObject{{QStringLiteral("min"), QStringLiteral("0.2.0")},
                                {QStringLiteral("max"), QStringLiteral("0.3.0")}});
    }
    fixture.install(QStringLiteral("fake-abi"), mode, json);
    PluginRegistryImpl registry;
    const PluginStatus status = loadOne(fixture, QStringLiteral("fake-abi"), registry);
    CHECK(status.state == PluginStatus::State::VersionMismatch);
    CHECK_FALSE(status.reason.isEmpty());
    CHECK(registry.loaded(PluginType::Output).isEmpty());
}

TEST_CASE("AC-03-0：這一版不支援的類型（decoder、dsp、ui、不認得的）載入時拒絕並說明") {
    for (const char* type : {"decoder", "dsp", "ui", "visualizer"}) {
        DYNAMIC_SECTION(type) {
            PluginFixture fixture(QStringLiteral("load-type"));
            QJsonObject json =
                PluginFixture::manifest(QStringLiteral("fake-type"), QStringLiteral("type.dll"));
            json.insert(QStringLiteral("type"), QString::fromLatin1(type));
            fixture.install(QStringLiteral("fake-type"), QStringLiteral("ok"), json);
            PluginRegistryImpl registry;
            const PluginStatus status = loadOne(fixture, QStringLiteral("fake-type"), registry);
            CHECK(status.state == PluginStatus::State::UnsupportedType);
            CHECK(status.reason.contains(QStringLiteral("這一版不支援")));
            CHECK(registry.loaded(PluginType::Output).isEmpty());
            CHECK(registry.loaded(PluginType::Decoder).isEmpty());
        }
    }
}

TEST_CASE("AC-03-0：進入點丟例外或回傳空值時停用該外掛並記原因，其他外掛照常載入") {
    PluginFixture fixture(QStringLiteral("load-fail"));
    fixture.install(
        QStringLiteral("fake-throw"), QStringLiteral("throw"),
        PluginFixture::manifest(QStringLiteral("fake-throw"), QStringLiteral("throw.dll")));
    fixture.install(
        QStringLiteral("fake-null"), QStringLiteral("null"),
        PluginFixture::manifest(QStringLiteral("fake-null"), QStringLiteral("null.dll")));
    fixture.install(
        QStringLiteral("fake-good"), QStringLiteral("ok"),
        PluginFixture::manifest(QStringLiteral("fake-good"), QStringLiteral("good.dll")));
    auto manager = fixture.manager();
    PluginRegistryImpl registry;
    REQUIRE_NOTHROW(manager.loadAll(registry));
    const auto list = manager.list();
    for (const char* id : {"fake-throw", "fake-null"}) {
        const PluginStatus* status = findStatus(list, QString::fromLatin1(id));
        REQUIRE(status != nullptr);
        CHECK(status->state == PluginStatus::State::LoadFailed);
        CHECK_FALSE(status->reason.isEmpty());
    }
    const auto loaded = registry.loaded(PluginType::Output);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front().id == QStringLiteral("fake-good"));
}

TEST_CASE("AC-03-0：停用清單裡的外掛不載入（已停用）") {
    PluginFixture fixture(QStringLiteral("load-disabled"));
    fixture.install(QStringLiteral("fake-off"), QStringLiteral("ok"),
                    PluginFixture::manifest(QStringLiteral("fake-off"), QStringLiteral("off.dll")));
    {
        QSettings settings(fixture.settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("plugins/disabled"),
                          QStringList{QStringLiteral("fake-off")});
    }
    PluginRegistryImpl registry;
    const PluginStatus status = loadOne(fixture, QStringLiteral("fake-off"), registry);
    CHECK(status.state == PluginStatus::State::Disabled);
    CHECK(registry.loaded(PluginType::Output).isEmpty());
}
