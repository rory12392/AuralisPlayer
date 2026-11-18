// 進階輸出模組的進入點：註冊 DSF／DFF 音源、DSD 包裝器（依 DSD 輸出方式）與其設定區塊、
// ReplayGain 處理器與其區塊、R128 計算、output 類型外掛的輸出路徑（ASIO 外掛，T-103）。
#include "dop_sink.h"
#include "dsd_mode_controller.h"
#include "dsd_source.h"
#include "native_dsd_sink.h"
#include "output_plugin_api.h"
#include "pcm_convert_sink.h"
#include "r128_controller.h"
#include "replay_gain_controller.h"
#include "replay_gain_processor.h"

#include <auralis/audio_output.h>
#include <auralis/audio_processor.h>
#include <auralis/audio_source.h>
#include <auralis/module.h>
#include <auralis/notifier.h>
#include <auralis/platform_window.h>
#include <auralis/plugins.h>
#include <auralis/ui_registry.h>

#include <QCoreApplication>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <vector>

// 設定頁與輸出面板的區塊（QML 模組 Auralis.Out，靜態外掛）
Q_IMPORT_QML_PLUGIN(Auralis_OutPlugin)

namespace auralis::mod::out {

namespace {

std::shared_ptr<DsdSink> makeDsdSink(DsdMode mode, quint32 pcmRate) {
    switch (mode) {
    case DsdMode::Native:
        return std::make_shared<NativeDsdSink>();
    case DsdMode::Pcm:
        return std::make_shared<PcmConvertSink>(pcmRate);
    case DsdMode::Dop:
        break;
    }
    return std::make_shared<DopSink>();
}

class HiresOutputModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("OUT"), QStringLiteral("進階輸出"), QStringLiteral("1")};
    }
    void start(ModuleHost& host) override {
        host.sources().add(std::make_shared<DsdSourceFactory>());
        // output 類型的外掛（T-103，第一個是 ASIO 外掛）：核心在模組啟動前載入完。
        // 有能收原生 DSD 的外掛時，DSD 輸出方式的「原生 DSD」才可選（沒有時停用，原因「需要 ASIO
        // 外掛」）
        std::vector<IOutputPlugin*> outputPlugins;
        bool nativeDsd = false;
        for (const LoadedPlugin& plugin : host.plugins().loaded(PluginType::Output)) {
            auto* output = static_cast<IOutputPlugin*>(plugin.api);
            outputPlugins.push_back(output);
            nativeDsd = nativeDsd || output->nativeDsd();
        }
        // DSD 輸出方式（T-36）：依 OUT/dsdMode 與輸出路徑註冊包裝器（建立時還沒有播放），
        // 之後換方式或換輸出時由它換；設定頁與輸出面板的區塊經 QML 單例 DsdOutput 操作它
        m_dsdMode =
            std::make_unique<DsdModeController>(host.settings(), host.outputs(), host.playback(),
                                                &host.notifier(), makeDsdSink, nativeDsd);
        DsdModeController::setInstance(m_dsdMode.get());
        host.ui().addSettingsSection(
            {QStringLiteral("dsd"), QCoreApplication::translate("HiresOutputModule", "DSD"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/DsdSettingsSection.qml")), 10});
        host.ui().addOutputPanelSection(
            {QStringLiteral("dsd"), QCoreApplication::translate("HiresOutputModule", "DSD 輸出"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/DsdPanelSection.qml")), 10});
        // ReplayGain（T-37）：設定與處理器共用原子變數；處理器替哪首算了什麼記在 log，給區塊說明。
        // 引擎在換曲時把曲目身分交給處理器（prepareTrack，T-49）
        auto rgSettings = std::make_shared<RgSettings>();
        auto rgLog = std::make_shared<RgAppliedLog>();
        m_replayGain = std::make_unique<ReplayGainController>(host.settings(), host.playback(),
                                                              rgSettings, rgLog);
        ReplayGainController::setInstance(m_replayGain.get());
        host.processors().add(std::make_shared<ReplayGainProcessorFactory>(
            rgSettings, libraryLookup(host.database(), host.library()), rgLog));
        host.ui().addSettingsSection(
            {QStringLiteral("replayGain"), QStringLiteral("ReplayGain"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/ReplayGainSettingsSection.qml")), 20});
        host.ui().addOutputPanelSection(
            {QStringLiteral("replayGain"), QStringLiteral("ReplayGain"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/ReplayGainPanelSection.qml")), 20});
        // R128 計算（T-38）：背景工作，結果只寫 rg_computed；設定頁的 ReplayGain 分區經單例 R128
        // 操作
        SourceRegistry& sources = host.sources();
        m_r128 = std::make_unique<R128Controller>(
            host.jobs(), host.library(), host.database(),
            [&sources](const TrackRef& track) { return sources.openTrack(track); },
            &host.notifier());
        R128Controller::setInstance(m_r128.get());
        // 曲目右鍵選單的「計算 ReplayGain」（T-88）：區塊收到選取曲目的 payloadJson，
        // 交給單例 R128。order 90：畫面規格 S-07 把它排在其他模組項目（加入播放清單、
        // 前往專輯…）之後
        host.ui().addTrackMenuSection(
            {QStringLiteral("r128"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Out/R128TrackMenuSection.qml")), 90});
        // 原生 DSD 不能用時的提示（AC-04-2）：後端在解碼或控制執行緒開啟，通知列只能在 UI 執行緒，
        // 所以排進主執行緒的事件迴圈；模組停止後才到的就丟掉
        Notifier* notifier = &host.notifier();
        auto alive = m_alive;
        auto notice = [notifier, alive](const QString& text) {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [notifier, alive, text] {
                    if (*alive) {
                        notifier->notify(text);
                    }
                },
                Qt::QueuedConnection);
        };
        // 外掛提供的輸出路徑。沒有安裝外掛時不註冊（輸出清單沒有 ASIO，AC-03-0）；
        // 有外掛但沒有安裝 ASIO 驅動時這個路徑沒有裝置，輸出清單也不出現 ASIO、不報錯（AC-03-1）
        for (IOutputPlugin* output : outputPlugins) {
            host.outputs().add(output->createFactory(host.platformWindow().hwnd(), notice));
        }
    }
    void stop() noexcept override {
        *m_alive = false;
        // 程式關閉：取消 R128 計算並等它停下（已算完的已寫入，正在算的不寫）
        if (m_r128) {
            m_r128->shutdown();
        }
    }

private:
    /// UI 執行緒讀寫：start() 之後為 true，stop() 設成 false。
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    /// 留到模組物件解構（QML 的單例參照它，stop() 時介面可能還在）。
    std::unique_ptr<DsdModeController> m_dsdMode;
    std::unique_ptr<ReplayGainController> m_replayGain;
    std::unique_ptr<R128Controller> m_r128;
};

} // namespace
} // namespace auralis::mod::out

AURALIS_REGISTER_MODULE(OUT, [] {
    return std::unique_ptr<auralis::IModule>(
        std::make_unique<auralis::mod::out::HiresOutputModule>());
})
