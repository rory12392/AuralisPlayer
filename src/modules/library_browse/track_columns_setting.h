#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class ModuleSettings;
} // namespace auralis

namespace auralis::mod::lib {

/// 曲目清單（所有曲目、資料夾頁共用）的欄位與欄寬，QML 以單例 LibraryColumns 取用
/// （T-90，AC-10-5）。
/// - saved：核心 TrackColumns.save() 的字串（欄位依顯示順序、固定寬的欄帶 width），存在
///   LIB/trackColumns，重新啟動後保留；空字串＝沒有存過（頁面用預設欄）。內容由頁面以
///   TrackColumns.restore() 解讀，這裡不檢查格式。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class TrackColumnsSetting : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryColumns)
    QML_SINGLETON
    Q_PROPERTY(QString saved READ saved WRITE setSaved NOTIFY savedChanged)

public:
    /// settings 為 nullptr 時只記在記憶體（測試用）。
    explicit TrackColumnsSetting(ModuleSettings* settings, QObject* parent = nullptr);

    QString saved() const { return m_saved; }
    void setSaved(const QString& value);

    /// 模組 start() 時設定、stop() 之後清掉；QML 引擎取單例時用它。
    static void setInstance(TrackColumnsSetting* instance);
    static TrackColumnsSetting* create(QQmlEngine*, QJSEngine*);

signals:
    void savedChanged();

private:
    ModuleSettings* m_settings;
    QString m_saved;
};

} // namespace auralis::mod::lib
