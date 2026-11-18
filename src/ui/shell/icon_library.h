#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace auralis::ui {

/// 圖示的線條資料（介面規範 §6.2）：讀 src/ui/icons/ 的 SVG（隨 QML 模組編進資源），
/// 把 <path> 與 <circle> 合成一條 SVG 路徑字串，交給共用元件 Icon 用 Shape／PathSvg 畫，
/// 顏色由 Icon 依 Theme 的 token 決定（圖檔本身不帶顏色）。只在 UI 執行緒使用。
// 由 QML 引擎建立（QML_SINGLETON），型別註冊會繼承它，所以不能標 final
class IconLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit IconLibrary(QObject* parent = nullptr);

    /// 圖示名稱（檔名去掉 .svg）對應的路徑字串；沒有這個圖示時回傳空字串並記警告。
    Q_INVOKABLE QString path(const QString& name);
    /// 所有圖示的名稱（依檔名排序），給元件展示頁列出全部圖示。
    Q_INVOKABLE QStringList names() const;

    /// 把一份 SVG 文字轉成路徑字串；只接受 <path d> 與 <circle cx cy r>（圖示只由直線與正圓構成）。
    /// 遇到其他圖形元素時回傳空字串。
    static QString pathFromSvg(const QByteArray& svg);

private:
    QHash<QString, QString> m_cache;
};

} // namespace auralis::ui
