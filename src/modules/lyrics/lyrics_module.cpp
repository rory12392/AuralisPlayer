// 歌詞與沉浸模組（LYR）的進入點。T-65：換歌時在背景依來源鏈找歌詞並寫快取；
// T-66：歌詞面板（panel/lyrics）與正在播放面板的歌詞預覽，資料經 QML 單例 Lyrics。
// T-67：LRCLIB 查詢（來源鏈最後一段）與設定頁的「歌詞」分區。T-68：全螢幕正在播放（fullscreen）。
// T-69：全螢幕底色依封面主色。
// modules/LYR/enabled=false 時核心不呼叫 start()，什麼都不做（面板與預覽都不登記）。
#include "backdrop.h"
#include "lrclib.h"
#include "lyrics_loader.h"
#include "lyrics_view.h"

#include <auralis/module.h>
#include <auralis/notifier.h>
#include <auralis/playback_service.h>
#include <auralis/ui_registry.h>

#include <QCoreApplication>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

// 歌詞面板與預覽（QML 模組 Auralis.Lyr，靜態外掛）
Q_IMPORT_QML_PLUGIN(Auralis_LyrPlugin)

namespace auralis::mod::lyr {

namespace {

class LyricsModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("LYR"), QStringLiteral("歌詞與沉浸"), QStringLiteral("1")};
    }

    void start(ModuleHost& host) override {
        m_loader = std::make_unique<LyricsLoader>(host.jobs(), host.library(), host.database());
        // T-67：LRCLIB 接在來源鏈最後；設定由單例 Lyrics 讀寫，經原子變數交給工作執行緒
        auto switches = std::make_shared<LrclibSwitches>();
        LrclibConfig config;
        config.userAgent = lrclibUserAgent(QCoreApplication::applicationVersion());
        // 另存 .lrc 失敗（工作執行緒）→ 通知列（只能在 UI 執行緒）；模組停止後才到的就丟掉
        Notifier* notifier = &host.notifier();
        auto alive = m_alive;
        m_loader->appendSource(std::make_unique<LrclibStep>(
            config, switches, [notifier, alive](const QString& reason) {
                QMetaObject::invokeMethod(
                    QCoreApplication::instance(),
                    [notifier, alive, reason] {
                        if (*alive) {
                            notifier->notify(
                                QCoreApplication::translate(
                                    "LyricsModule", "無法把歌詞另存為 .lrc（%1）。歌詞照常顯示。")
                                    .arg(reason),
                                Notifier::Kind::Error);
                        }
                    },
                    Qt::QueuedConnection);
            }));
        // T-69：全螢幕底色＝目前曲目封面主色壓暗（AC-17-2），在背景工作取色
        m_backdrop = std::make_unique<BackdropLoader>(host.jobs(), host.library(), host.covers());
        PlaybackService& playback = host.playback();
        QObject::connect(&playback, &PlaybackService::trackChanged, m_loader.get(),
                         [this, &playback] {
                             if (const auto entry = playback.currentEntry()) {
                                 m_loader->load(entry->track);
                                 m_backdrop->load(entry->track);
                             } else {
                                 m_loader->clear();
                                 m_backdrop->clear();
                             }
                         });
        m_view = std::make_unique<LyricsView>(*m_loader, playback, &host.settings(), switches);
        m_view->setBackdrop(m_backdrop.get());
        LyricsView::setInstance(m_view.get());
        // 設定頁的「歌詞與全螢幕」分區（S-16）：排在 OUT 的 DSD 10、ReplayGain 20 之後
        host.ui().addSettingsSection(
            {QStringLiteral("lyrics"), QCoreApplication::translate("LyricsModule", "歌詞與全螢幕"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lyr/LyricsSettingsSection.qml")), 30});
        // 面板頁籤順序：正在播放 10、歌詞 20、佇列 30（畫面規格 S-12）
        host.ui().addPanel({QStringLiteral("panel/lyrics"),
                            QCoreApplication::translate("LyricsModule", "歌詞"),
                            QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lyr/LyricsPanel.qml")), 20});
        host.ui().addNowPlayingSection(
            {QStringLiteral("lyrics/preview"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lyr/LyricsPreview.qml")), 20});
        // T-68：全螢幕正在播放（S-15，頁面 id fullscreen）；F11、Esc、開在哪個螢幕由核心處理
        host.ui().addFullscreenPage(
            {QStringLiteral("fullscreen"),
             QCoreApplication::translate("LyricsModule", "全螢幕正在播放"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lyr/FullscreenPage.qml")), 10});
    }

    void stop() noexcept override {
        *m_alive = false;
        // 核心在模組停止後先解構音樂庫、資料庫、封面服務，最後才是 JobRunner，所以這裡要等
        // 自己的工作結束才刪載入器（P-LYR-10）。等的時候不處理事件，已停止的回呼不會跑
        if (m_loader) {
            m_loader->shutdown(kStopTimeoutMs);
        }
        if (m_backdrop) {
            m_backdrop->shutdown(kStopTimeoutMs);
        }
        m_loader.reset();
        m_backdrop.reset();
    }

private:
    /// 停止時等背景工作的上限：LRCLIB 一次請求的逾時 5 秒加餘裕。取消後查詢會立刻 abort，
    /// 正常不會等到這麼久；這是工作卡在別的地方時的保險。
    static constexpr int kStopTimeoutMs = 7000;
    /// 只在 UI 執行緒讀寫（排進主執行緒的通知看它決定要不要顯示）。
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<LyricsLoader> m_loader;
    std::unique_ptr<BackdropLoader> m_backdrop;
    /// 留到模組物件解構（QML 的單例參照它，停止時介面可能還在）；
    /// 載入器刪掉後它只顯示「沒有歌詞」。
    std::unique_ptr<LyricsView> m_view;
};

} // namespace

} // namespace auralis::mod::lyr

AURALIS_REGISTER_MODULE(LYR, []() -> std::unique_ptr<auralis::IModule> {
    return std::make_unique<auralis::mod::lyr::LyricsModule>();
})
