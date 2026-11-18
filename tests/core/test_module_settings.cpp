// 模組設定：只讀寫 settings.ini 的 <模組代碼>/ 段，重新開啟後保留。
#include "settings/module_settings_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QTemporaryDir>

using auralis::core::ModuleSettingsImpl;

namespace {

QString testRoot() {
    return QString::fromUtf8(AURALIS_TEST_TMP);
}

} // namespace

TEST_CASE("設定：模組的鍵寫在自己的段落，重新開啟後保留") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/settings-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    {
        ModuleSettingsImpl lyrics(path, QStringLiteral("LYR"));
        CHECK_FALSE(lyrics.contains(QStringLiteral("online")));
        CHECK(lyrics.value(QStringLiteral("online"), true).toBool());
        lyrics.setValue(QStringLiteral("online"), false);
    }
    const QSettings raw(path, QSettings::IniFormat);
    CHECK(raw.contains(QStringLiteral("LYR/online")));
    CHECK_FALSE(raw.value(QStringLiteral("LYR/online")).toBool());

    ModuleSettingsImpl reopened(path, QStringLiteral("LYR"));
    CHECK(reopened.contains(QStringLiteral("online")));
    CHECK_FALSE(reopened.value(QStringLiteral("online"), true).toBool());
    reopened.remove(QStringLiteral("online"));
    CHECK_FALSE(reopened.contains(QStringLiteral("online")));
}

TEST_CASE("設定：模組之間看不到彼此的鍵，也碰不到模組啟用設定") {
    QTemporaryDir dir(testRoot() + QStringLiteral("/settings-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    QSettings(path, QSettings::IniFormat).setValue(QStringLiteral("modules/PL/enabled"), true);

    ModuleSettingsImpl playlists(path, QStringLiteral("PL"));
    ModuleSettingsImpl output(path, QStringLiteral("OUT"));
    playlists.setValue(QStringLiteral("lastFolder"), QStringLiteral("D:\\清單"));
    CHECK_FALSE(output.contains(QStringLiteral("lastFolder")));
    CHECK_FALSE(playlists.contains(QStringLiteral("enabled")));

    playlists.setValue(QStringLiteral("enabled"), false);
    CHECK(
        QSettings(path, QSettings::IniFormat).value(QStringLiteral("modules/PL/enabled")).toBool());
}
