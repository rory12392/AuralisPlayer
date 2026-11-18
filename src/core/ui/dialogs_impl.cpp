#include "ui/dialogs_impl.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace auralis::core {

namespace {

/// QML 的檔案對話框回傳 file: 網址；轉成本機路徑（Windows 分隔符）。
QStringList toPaths(const QVariant& result) {
    QStringList paths;
    for (const QVariant& item : result.toList()) {
        const QString text = item.toString();
        const QUrl url = item.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : text;
        if (!path.isEmpty()) {
            paths << QDir::toNativeSeparators(path);
        }
    }
    return paths;
}

} // namespace

DialogsImpl::DialogsImpl(QObject* parent) : QObject(parent) {}

int DialogsImpl::request(const QString& kind, const QVariantMap& options,
                         std::function<void(const QVariant&)> done) {
    const int id = m_nextId++;
    m_pending.emplace(id, std::move(done));
    emit requested(id, kind, options);
    return id;
}

void DialogsImpl::chooseFolder(const QString& title, PathsCallback done) {
    request(QStringLiteral("folder"), {{QStringLiteral("title"), title}},
            [done](const QVariant& result) { done(toPaths(result)); });
}

void DialogsImpl::chooseFiles(const QString& title, const QStringList& nameFilters,
                              PathsCallback done) {
    request(QStringLiteral("files"),
            {{QStringLiteral("title"), title}, {QStringLiteral("nameFilters"), nameFilters}},
            [done](const QVariant& result) { done(toPaths(result)); });
}

void DialogsImpl::chooseSaveFile(const QString& title, const QString& suggestedName,
                                 const QStringList& nameFilters, PathsCallback done) {
    QVariantMap options{{QStringLiteral("title"), title},
                        {QStringLiteral("suggestedName"), suggestedName},
                        {QStringLiteral("nameFilters"), nameFilters}};
    // 完整路徑：QML 的檔案對話框要 file: 網址，資料夾另外給（不用系統記得的上一個資料夾）
    if (QDir::isAbsolutePath(suggestedName)) {
        const QFileInfo file(suggestedName);
        options.insert(QStringLiteral("folder"), QUrl::fromLocalFile(file.absolutePath()));
        options.insert(QStringLiteral("suggestedName"),
                       QUrl::fromLocalFile(file.absoluteFilePath()));
    }
    request(QStringLiteral("saveFile"), options,
            [done](const QVariant& result) { done(toPaths(result).mid(0, 1)); });
}

void DialogsImpl::confirm(const ConfirmRequest& confirmRequest, std::function<void(bool)> done) {
    request(QStringLiteral("confirm"),
            {{QStringLiteral("title"), confirmRequest.title},
             {QStringLiteral("message"), confirmRequest.message},
             {QStringLiteral("confirmText"), confirmRequest.confirmText},
             {QStringLiteral("destructive"), confirmRequest.destructive}},
            [done](const QVariant& result) { done(result.toBool()); });
}

void DialogsImpl::respond(int id, const QVariant& result) {
    const auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    // 先移出再呼叫：回呼裡可能再開下一個對話框
    auto done = std::move(it->second);
    m_pending.erase(it);
    done(result);
}

NotifierImpl::NotifierImpl(QObject* parent) : QObject(parent) {}

void NotifierImpl::notify(const QString& text, Kind kind, const QString& actionText,
                          std::function<void()> action) {
    m_current = {{QStringLiteral("id"), m_nextId++},
                 {QStringLiteral("text"), text},
                 {QStringLiteral("kind"),
                  kind == Kind::Error ? QStringLiteral("error") : QStringLiteral("info")},
                 {QStringLiteral("actionText"), action ? actionText : QString()}};
    m_action = std::move(action);
    emit currentChanged();
}

void NotifierImpl::post(const QString& text, const QString& kind) {
    notify(text, kind == QLatin1String("error") ? Kind::Error : Kind::Info);
}

void NotifierImpl::triggerAction(int id) {
    if (m_current.value(QStringLiteral("id")).toInt() != id) {
        return;
    }
    auto action = std::move(m_action);
    dismiss(id);
    if (action) {
        action();
    }
}

void NotifierImpl::dismiss(int id) {
    if (m_current.value(QStringLiteral("id")).toInt() != id) {
        return;
    }
    m_current.clear();
    m_action = {};
    emit currentChanged();
}

} // namespace auralis::core
