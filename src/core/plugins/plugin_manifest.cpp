#include "plugins/plugin_manifest.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace auralis::core {

namespace {

/// plugin.json 只有幾百位元組；設上限，避免讀進奇怪的大檔。
constexpr qint64 kMaxManifestBytes = 64 * 1024;

Error manifestError(const QString& message) {
    return Error{message, 0, QStringLiteral("readPluginManifest")};
}

/// 必填的字串欄位；缺少或不是非空字串時回傳 false。
bool readString(const QJsonObject& object, const char* key, QString& out) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        return false;
    }
    out = value.toString().trimmed();
    return true;
}

} // namespace

QString pluginFilePath(const QString& folder, const QString& name) {
    const QString path = folder + QLatin1Char('/') + name;
    return !name.isEmpty() && QFileInfo(path).isFile() ? path : QString();
}

bool isValidPluginId(const QString& id) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

Result<PluginManifest> readPluginManifest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return manifestError(QStringLiteral("讀不到 plugin.json：%1").arg(file.errorString()));
    }
    if (file.size() > kMaxManifestBytes) {
        return manifestError(QStringLiteral("plugin.json 太大"));
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return manifestError(
            QStringLiteral("plugin.json 格式錯誤：%1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return manifestError(QStringLiteral("plugin.json 格式錯誤：不是物件"));
    }
    const QJsonObject object = document.object();
    PluginManifest manifest;
    const struct {
        const char* key;
        QString* out;
    } strings[] = {{"id", &manifest.id},     {"name", &manifest.name},
                   {"type", &manifest.type}, {"version", &manifest.version},
                   {"dll", &manifest.dll},   {"license", &manifest.license},
                   {"url", &manifest.url}};
    for (const auto& field : strings) {
        if (!readString(object, field.key, *field.out)) {
            return manifestError(
                QStringLiteral("plugin.json 缺少 %1").arg(QString::fromLatin1(field.key)));
        }
    }
    const QJsonObject app = object.value(QStringLiteral("appVersion")).toObject();
    if (!readString(app, "min", manifest.appMin) || !readString(app, "max", manifest.appMax)) {
        return manifestError(QStringLiteral("plugin.json 缺少 appVersion 的 min 或 max"));
    }
    const QJsonValue abi = object.value(QStringLiteral("abi"));
    if (!abi.isDouble() || abi.toDouble() < 1 || abi.toDouble() != abi.toInt()) {
        return manifestError(QStringLiteral("plugin.json 缺少 abi（正整數）"));
    }
    manifest.abi = abi.toInt();
    if (!isValidPluginId(manifest.id)) {
        return manifestError(QStringLiteral("plugin.json 的 id 只能用英數字、- 與 _"));
    }
    // DLL 一定在 plugin.json 旁邊：不接受路徑，載入時才不會被帶到資料夾外
    static const QRegularExpression dllName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*\\.dll$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (!dllName.match(manifest.dll).hasMatch() || manifest.dll.contains(QStringLiteral(".."))) {
        return manifestError(QStringLiteral("plugin.json 的 dll 只能是檔名（.dll）"));
    }
    // 標誌（選填）：和 DLL 一樣只能是外掛資料夾裡的檔名
    if (object.contains(QStringLiteral("logo"))) {
        static const QRegularExpression logoName(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*\\.(svg|png)$"),
            QRegularExpression::CaseInsensitiveOption);
        const QJsonObject logo = object.value(QStringLiteral("logo")).toObject();
        if (!readString(logo, "dark", manifest.logoDark) ||
            !readString(logo, "light", manifest.logoLight) ||
            !logoName.match(manifest.logoDark).hasMatch() ||
            !logoName.match(manifest.logoLight).hasMatch() ||
            manifest.logoDark.contains(QStringLiteral("..")) ||
            manifest.logoLight.contains(QStringLiteral(".."))) {
            return manifestError(QStringLiteral(
                "plugin.json 的 logo 要有 dark 與 light，只能是檔名（.svg 或 .png）"));
        }
    }
    return manifest;
}

} // namespace auralis::core
