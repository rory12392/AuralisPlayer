#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class PlaybackService;
} // namespace auralis

namespace auralis::mod::lib {

class SearchController;

/// 「所有曲目」頁（S-07）的播放動作與正在播放的標記，QML 以單例 LibraryPlayback 取用。
/// - playFrom(row)：佇列換成 LibrarySearch 目前已載入的整份清單，從第 row 首開始播
///   （PlaybackService::playFrom，雙擊或 Enter）。
/// - currentKey：正在聽的曲目的鍵（keyOf(path, subsong)），清單列比對它畫琥珀方點；沒有時空字串。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class LibraryPlayback : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryPlayback)
    QML_SINGLETON
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentKeyChanged)

public:
    LibraryPlayback(PlaybackService& playback, SearchController& search, QObject* parent = nullptr);

    QString currentKey() const { return m_currentKey; }

    /// 從清單第 row 首開始播放；row 不在已載入的範圍時不做事、回傳 false。
    Q_INVOKABLE bool playFrom(int row);
    /// 曲目的鍵：路徑與子曲目編號（CUE 的同一個檔案有好幾首）。
    Q_INVOKABLE static QString keyOf(const QString& path, int subsong);

    /// 模組 start() 時設定、stop() 之後清掉；QML 引擎取單例時用它。
    static void setInstance(LibraryPlayback* instance);
    static LibraryPlayback* create(QQmlEngine*, QJSEngine*);

signals:
    void currentKeyChanged();

private:
    void updateCurrent();

    PlaybackService& m_playback;
    SearchController& m_search;
    QString m_currentKey;
};

} // namespace auralis::mod::lib
