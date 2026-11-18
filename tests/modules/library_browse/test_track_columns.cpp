// 曲目清單的欄位與欄寬設定（T-90，US-10 AC-5）：存在 LIB/trackColumns，重新建立後保留。
#include "lib_test_helpers.h"
#include "settings/module_settings_impl.h"
#include "track_columns_setting.h"

#include <auralis/module_settings.h>

#include <QFile>
#include <QHash>

#include <catch2/catch_test_macros.hpp>

using namespace auralis;
using namespace auralis::test;
using namespace auralis::mod::lib;

namespace {

class MemorySettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return m_values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { m_values[key] = value; }
    bool contains(const QString& key) const override { return m_values.contains(key); }
    void remove(const QString& key) override { m_values.remove(key); }

private:
    QHash<QString, QVariant> m_values;
};

} // namespace

TEST_CASE("AC-10-5：欄位與欄寬存在 LIB/trackColumns，重新建立後保留") {
    ensureApp();
    MemorySettings settings;
    const QString saved = QStringLiteral(R"([{"id":"title"},{"id":"year","width":56}])");
    {
        TrackColumnsSetting columns(&settings);
        CHECK(columns.saved().isEmpty()); // 沒存過：頁面用預設欄
        int changed = 0;
        QObject::connect(&columns, &TrackColumnsSetting::savedChanged, [&changed] { ++changed; });
        columns.setSaved(saved);
        columns.setSaved(saved); // 同樣的值不再通知、不再寫
        CHECK(changed == 1);
        CHECK(settings.value(QStringLiteral("trackColumns"), {}).toString() == saved);
    }
    TrackColumnsSetting again(&settings);
    CHECK(again.saved() == saved);
}

TEST_CASE("AC-10-5：寫進真的 settings.ini（LIB 段），新的實例（重新啟動）讀回同一份") {
    ensureApp();
    const QString path = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-90-settings.ini");
    QFile::remove(path);
    const QString saved = QStringLiteral(R"([{"id":"title"},{"id":"path"}])");
    {
        core::ModuleSettingsImpl settings(path, QStringLiteral("LIB"));
        TrackColumnsSetting columns(&settings);
        columns.setSaved(saved);
    }
    core::ModuleSettingsImpl settings(path, QStringLiteral("LIB"));
    CHECK(settings.value(QStringLiteral("trackColumns")).toString() == saved);
    TrackColumnsSetting again(&settings);
    CHECK(again.saved() == saved);
    QFile::remove(path);
}

TEST_CASE("T-90：沒有設定（測試用）時只記在記憶體") {
    ensureApp();
    TrackColumnsSetting columns(nullptr);
    columns.setSaved(QStringLiteral("[]"));
    CHECK(columns.saved() == QStringLiteral("[]"));
}
