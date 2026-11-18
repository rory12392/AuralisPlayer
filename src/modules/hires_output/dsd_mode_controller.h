#pragma once

#include "dsd_mode.h"

#include <auralis/dsd.h>
#include <auralis/playback_service.h>

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class ModuleSettings;
class Notifier;
class OutputRegistry;
} // namespace auralis

namespace auralis::mod::out {

/// 依方式與轉 PCM 取樣率建立 DSD 包裝器（模組的 start() 給，測試換成記錄用的）。
using DsdSinkMaker = std::function<std::shared_ptr<DsdSink>(DsdMode mode, quint32 pcmRate)>;

/// DSD 輸出方式（設定頁「DSD」分區與輸出面板的「DSD 輸出」區塊，S-14、S-16；AC-04-2、AC-02-6）。
/// QML 以單例 DsdOutput 取用。記住使用者選的方式（OUT/dsdMode）與轉 PCM 取樣率（OUT/dsdPcmRate），
/// 依目前的輸出路徑算出實際能用的方式，向核心註冊對應的包裝器：
/// - 換方式或換到別的輸出路徑而實際方式改變時換包裝器；DSD 曲目播放中時先暫停（記下位置、關裝置）、
///   換包裝器、再從原位置繼續（規格 §1 情況 2）。核心的 setDsdSink 只在引擎停著時交給引擎。
///   播放中換輸出時核心先發 outputPathChanging（引擎已停），這時只換包裝器、不暫停重播。
/// - 輸出路徑由畫面（輸出面板與設定頁的裝置清單，選的那列）與輸出狀態告訴它，記在 OUT/outputPath，
///   下次啟動在開始播放前就選對包裝器。
/// - DSD 曲目因為裝置不支援而被跳過時（例：DSD256 經 WASAPI 獨占的 DoP 要 705.6 kHz），
///   區塊下方用 signal 寫原因與建議，並送通知（AC-04-2、規格 §1 情況 3）。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class DsdModeController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DsdOutput)
    QML_SINGLETON
    /// 實際使用的方式（0 DoP、1 原生 DSD、2 轉 PCM），SegmentedControl 的 currentIndex。
    Q_PROPERTY(int modeIndex READ modeIndex NOTIFY changed)
    /// SegmentedControl 的 model：[{ text, enabled, reason }]，不能用的方式停用並寫原因。
    Q_PROPERTY(QVariantList options READ options NOTIFY changed)
    /// 轉 PCM 取樣率在 pcmRateLabels 的位置。
    Q_PROPERTY(int pcmRateIndex READ pcmRateIndex NOTIFY changed)
    Q_PROPERTY(QStringList pcmRateLabels READ pcmRateLabels CONSTANT)
    /// 選的方式在目前的輸出路徑不能用、改用別的方式時的說明；沒有時空字串。
    Q_PROPERTY(QString note READ note NOTIFY changed)
    /// 裝置不支援時的原因與建議（signal）；換方式、換輸出或下一首 DSD 播得出來時清掉。
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    /// 正在播的 DSD 曲目用哪種方式送出（「DoP」「原生」「轉 PCM」）；不是 DSD 時空字串。
    Q_PROPERTY(QString routeLabel READ routeLabel NOTIFY changed)
    /// 有能收原生 DSD 的輸出外掛（ASIO 外掛）；沒有時區塊提供到設定頁「外掛」分區的按鈕（T-104）。
    Q_PROPERTY(bool nativeDsdAvailable READ nativeDsdAvailable CONSTANT)

public:
    /// notifier 為 nullptr 時不送通知（測試用）。建立時就依設定註冊包裝器（這時還沒有播放）。
    /// nativeDsd：有沒有能收原生 DSD 的輸出外掛（T-103）；false 時「原生 DSD」停用、原因「需要 ASIO
    /// 外掛」。
    DsdModeController(ModuleSettings& settings, OutputRegistry& outputs, PlaybackService& playback,
                      Notifier* notifier, DsdSinkMaker makeSink, bool nativeDsd = true,
                      QObject* parent = nullptr);
    ~DsdModeController() override;

    int modeIndex() const { return static_cast<int>(effective()); }
    QVariantList options() const;
    int pcmRateIndex() const;
    QStringList pcmRateLabels() const;
    QString note() const;
    QString warning() const { return m_warning; }
    QString routeLabel() const { return dsdRouteLabel(m_playback.outputStatus()); }
    bool nativeDsdAvailable() const { return m_nativeDsd; }

    DsdMode chosenMode() const { return m_mode; }
    DsdMode effective() const {
        return effectiveMode(m_mode, outputKind(m_outputPath), m_nativeDsd);
    }
    quint32 pcmRate() const { return m_pcmRate; }

    /// 使用者選了第 index 個方式（不能用的方式不接受）。
    Q_INVOKABLE void selectMode(int index);
    /// 使用者選了 pcmRateLabels 的第 index 個取樣率。
    Q_INVOKABLE void selectPcmRate(int index);
    /// 目前選的輸出路徑（輸出後端工廠的名稱）；空字串不理會。
    Q_INVOKABLE void setOutputPath(const QString& backendName);

    static void setInstance(DsdModeController* instance);
    static DsdModeController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    void onStatus(const OutputStatus& status);
    void onSkipped(const TrackRef& track, const QString& reason);
    /// 核心換輸出路徑前的通知（PlaybackService::outputPathChanging）：記住路徑、換包裝器，
    /// 不暫停與重播（核心正在換輸出，之後用新的包裝器從原位置繼續）。
    void onPathChanging(const QString& backend);
    /// 實際方式或取樣率和已註冊的不同時換包裝器。allowRestart 時 DSD 播放中先暫停、換完從原位置
    /// 繼續；核心換輸出途中（onPathChanging）不可，傳 false。
    void apply(bool allowRestart = true);
    void setWarning(const QString& warning);

    ModuleSettings& m_settings;
    OutputRegistry& m_outputs;
    PlaybackService& m_playback;
    Notifier* m_notifier;
    DsdSinkMaker m_makeSink;
    bool m_nativeDsd = true;
    DsdMode m_mode = DsdMode::Dop;
    quint32 m_pcmRate = 176400;
    QString m_outputPath;
    QString m_warning;
    /// 已註冊的包裝器是哪個方式、哪個取樣率（轉 PCM 以外是 0）。
    bool m_registered = false;
    DsdMode m_registeredMode = DsdMode::Dop;
    quint32 m_registeredRate = 0;
};

} // namespace auralis::mod::out
