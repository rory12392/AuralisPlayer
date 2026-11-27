#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class ModuleSettings;
class PlaybackService;
} // namespace auralis

namespace auralis::mod::lyr {

class BackdropLoader;
class LyricsLoader;
struct LrclibSwitches;

/// 歌詞面板（S-12）、正在播放面板的歌詞預覽（S-11）與之後全螢幕（T-68）共用的歌詞狀態。
/// QML 以單例 Lyrics 取用。目前行跟著 PlaybackService::positionChanged（每 50 毫秒、跳轉時也發）
/// 用二分搜尋更新（AC-15-2）；點一行跳到該行時間（offset 已套用，AC-15-3）。
/// 執行緒：只在 UI 執行緒使用。載入器由模組擁有、在 stop() 時刪掉，所以只留 QPointer。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class LyricsView : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Lyrics)
    QML_SINGLETON
    /// "loading"、"synced"、"plain"、"none"、"instrumental"（純音樂）、
    /// "unavailable"（線上歌詞暫時無法取得，T-67）。
    Q_PROPERTY(QString state READ state NOTIFY changed)
    /// 每行的文字（同步歌詞依時間排序）。
    Q_PROPERTY(QStringList lines READ lines NOTIFY changed)
    /// 歌詞來源："lrc"、"embedded"、"lrclib"；沒有歌詞時空字串。
    Q_PROPERTY(QString source READ source NOTIFY changed)
    /// 設定 LYR/online（線上抓取，預設開）；關閉時「沒有歌詞」多一句說明。
    Q_PROPERTY(bool onlineEnabled READ onlineEnabled NOTIFY changed)
    /// 設定 LYR/saveLrc（把抓到的歌詞另存為同名 .lrc，預設關，AC-16-4）。
    Q_PROPERTY(bool saveLrc READ saveLrc NOTIFY changed)
    /// 目前播放到第幾行；第一行之前或不是同步歌詞時 -1。
    Q_PROPERTY(int currentLine READ currentLine NOTIFY currentLineChanged)
    /// 全螢幕正在播放（S-15）的底色：目前曲目封面主色壓暗後的值（BackdropLoader，在 C++ 算，
    /// QML 不寫色碼）；沒有封面、取色失敗或沒有接底色時 #050505。換歌時結果回來才變（T-69）。
    /// 值是「#rrggbb」字串（本模組不連結 Qt Gui；QML 的 color 屬性直接接受）。
    Q_PROPERTY(QString fullscreenBackdrop READ fullscreenBackdrop NOTIFY fullscreenBackdropChanged)
    /// 淺色全螢幕的底色來源（AC-17-6）：封面主色原值「#rrggbb」（沒壓暗），沒有封面時空字串；
    /// QML 交給 ThemeController.lightBackdrop 提亮。和 fullscreenBackdrop 一起通知。
    Q_PROPERTY(
        QString fullscreenCoverColor READ fullscreenCoverColor NOTIFY fullscreenBackdropChanged)
    /// 設定 LYR/fullscreenTheme（AC-17-6）：dark（預設）、light、follow；設定值無效時 dark。
    /// 全螢幕頁把它宣告成自己的 fullscreenTheme，核心照它套用主題。
    Q_PROPERTY(QString fullscreenTheme READ fullscreenTheme NOTIFY fullscreenThemeChanged)

public:
    /// settings 可以是 nullptr（測試），這時線上抓取視為開啟。switches 是 LRCLIB 那段讀的
    /// 設定（可以是 nullptr）：設定頁改了就同步過去。
    LyricsView(LyricsLoader& loader, PlaybackService& playback, ModuleSettings* settings,
               std::shared_ptr<LrclibSwitches> switches = nullptr, QObject* parent = nullptr);
    ~LyricsView() override;

    QString state() const { return m_state; }
    QStringList lines() const { return m_lines; }
    QString source() const { return m_source; }
    bool onlineEnabled() const { return m_onlineEnabled; }
    bool saveLrc() const { return m_saveLrc; }
    int currentLine() const { return m_currentLine; }
    QString fullscreenBackdrop() const;
    QString fullscreenCoverColor() const;
    QString fullscreenTheme() const { return m_fullscreenTheme; }
    /// 全螢幕頁的 T 鍵與切換鈕、設定頁（S-15、S-16）：寫進 LYR/fullscreenTheme，立刻生效；
    /// 不認得的值存成 "dark"。
    Q_INVOKABLE void setFullscreenTheme(const QString& theme);
    /// 全螢幕底色的來源（模組在 start() 接；可以是 nullptr）。底色變了發出
    /// fullscreenBackdropChanged。
    void setBackdrop(BackdropLoader* backdrop);

    /// 跳到第 index 行的時間（AC-15-3）；不是同步歌詞或超出範圍時不做事。
    Q_INVOKABLE void seekToLine(int index);
    /// 設定頁（S-16）：線上抓取開關，立刻生效：目前這首重找（關掉時進行中的查詢 abort）。
    Q_INVOKABLE void setOnlineEnabled(bool enabled);
    /// 設定頁（S-16）：另存 .lrc 開關，下一次抓到時生效。
    Q_INVOKABLE void setSaveLrc(bool enabled);
    /// 面板「重試」（AC-16-2）：重找目前這首。
    Q_INVOKABLE void retry();

    static void setInstance(LyricsView* instance);
    static LyricsView* create(QQmlEngine*, QJSEngine*);

signals:
    /// state、lines、source、onlineEnabled 改變（換歌、找完歌詞）。
    void changed();
    void currentLineChanged();
    void fullscreenBackdropChanged();
    void fullscreenThemeChanged();

private:
    /// 依載入器目前的結果重設內容。
    void reload();
    void updateLine(qint64 positionMs);

    /// 讀設定並同步給 LRCLIB 那段。
    void readSettings();

    QPointer<LyricsLoader> m_loader;
    QPointer<BackdropLoader> m_backdrop;
    PlaybackService& m_playback;
    ModuleSettings* m_settings;
    std::shared_ptr<LrclibSwitches> m_switches;
    QString m_state = QStringLiteral("none");
    QStringList m_lines;
    QList<qint64> m_times;
    QString m_source;
    bool m_onlineEnabled = true;
    bool m_saveLrc = false;
    int m_currentLine = -1;
    QString m_fullscreenTheme = QStringLiteral("dark");
};

} // namespace auralis::mod::lyr
