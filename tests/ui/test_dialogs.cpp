// 核心 Dialogs 的另存新檔請求（M-04 驗證的 F-3）：建議檔名是完整路徑時，對話框從那個資料夾開始，
// 不落在系統記得的上一個資料夾（可能是使用者的音樂資料夾）。系統對話框不問覆蓋（F-2）在
// tst_dialog_host.qml。
#include "ui/dialogs_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QUrl>

using auralis::core::DialogsImpl;

namespace {

struct Captured {
    QString kind;
    QVariantMap options;
};

Captured requestSave(DialogsImpl& dialogs, const QString& suggestedName) {
    Captured captured;
    const auto connection =
        QObject::connect(&dialogs, &DialogsImpl::requested,
                         [&captured](int, const QString& kind, const QVariantMap& options) {
                             captured = {kind, options};
                         });
    dialogs.chooseSaveFile(QStringLiteral("匯出播放清單"), suggestedName,
                           {QStringLiteral("播放清單 (*.m3u8)")}, [](const QStringList&) {});
    QObject::disconnect(connection);
    return captured;
}

} // namespace

TEST_CASE("F-3：另存新檔的建議檔名是完整路徑時，對話框從那個資料夾開始、預填那個檔名") {
    DialogsImpl dialogs;
    const QString folder = QDir::toNativeSeparators(QDir::tempPath()) + QStringLiteral("\\匯出");
    const Captured full = requestSave(dialogs, folder + QStringLiteral("\\通勤.m3u8"));
    CHECK(full.kind == QStringLiteral("saveFile"));
    CHECK(full.options.value(QStringLiteral("folder")).toUrl() == QUrl::fromLocalFile(folder));
    CHECK(full.options.value(QStringLiteral("suggestedName")).toUrl() ==
          QUrl::fromLocalFile(folder + QStringLiteral("\\通勤.m3u8")));

    // 只有檔名時照舊：不指定資料夾
    const Captured nameOnly = requestSave(dialogs, QStringLiteral("通勤.m3u8"));
    CHECK_FALSE(nameOnly.options.contains(QStringLiteral("folder")));
    CHECK(nameOnly.options.value(QStringLiteral("suggestedName")).toString() ==
          QStringLiteral("通勤.m3u8"));
}
