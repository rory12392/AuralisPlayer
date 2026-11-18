#include "shell/file_location.h"

#include "shell/core_bindings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QThreadPool>

#include <utility>

#include <windows.h>

#include <shlobj.h>

namespace {

Q_LOGGING_CATEGORY(lcFileLocation, "auralis.ui")

auralis::ui::FileLocation::RevealHook& revealHook() {
    static auralis::ui::FileLocation::RevealHook hook;
    return hook;
}

auralis::ui::FileLocation::ExistsHook& existsHook() {
    static auralis::ui::FileLocation::ExistsHook hook;
    return hook;
}

/// 會碰檔案系統（可能等很久），在背景執行緒呼叫。
bool fileExists(const QString& path) {
    if (existsHook()) {
        return existsHook()(path);
    }
    const QFileInfo info(path);
    return !path.isEmpty() && info.exists() && info.isFile();
}

/// SHOpenFolderAndSelectItems：開（或重用）檔案所在資料夾的視窗並選取它，不經命令列，路徑有空白、逗號也不用跳脫。
bool revealInExplorer(const QString& path) {
    const std::wstring native =
        QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(native.c_str());
    if (item == nullptr) {
        qCWarning(lcFileLocation) << "開啟檔案位置：無法取得" << path;
        return false;
    }
    const HRESULT result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    if (FAILED(result)) {
        qCWarning(lcFileLocation) << "開啟檔案位置失敗" << path << Qt::hex
                                  << static_cast<quint32>(result);
        return false;
    }
    return true;
}

} // namespace

namespace auralis::ui {

FileLocation::FileLocation(QObject* parent) : QObject(parent) {}

bool FileLocation::exists(const QString& path) const {
    return fileExists(path);
}

void FileLocation::reveal(const QString& path) {
    const QPointer<FileLocation> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        const bool found = fileExists(path);
        // 回到 UI 執行緒：用 QCoreApplication 當接收者，FileLocation 先被刪掉時也不會碰到懸空指標
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, path, found] {
                if (self) {
                    self->finishReveal(path, found);
                }
            },
            Qt::QueuedConnection);
    });
}

void FileLocation::finishReveal(const QString& path, bool found) {
    bool shown = false;
    if (!found) {
        qCWarning(lcFileLocation) << "開啟檔案位置：檔案不存在" << path;
        if (core::NotifierImpl* notifier = detail::notifier()) {
            notifier->post(tr("找不到檔案：%1").arg(QFileInfo(path).fileName()),
                           QStringLiteral("error"));
        }
    } else if (revealHook()) {
        shown = revealHook()(path);
    } else {
        shown = revealInExplorer(path);
    }
    emit revealFinished(path, shown);
}

void FileLocation::setRevealHook(RevealHook hook) {
    revealHook() = std::move(hook);
}

void FileLocation::setExistsHook(ExistsHook hook) {
    existsHook() = std::move(hook);
}

} // namespace auralis::ui
