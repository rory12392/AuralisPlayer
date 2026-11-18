#pragma once

#include <QJsonObject>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <atomic>
#include <memory>

class QThread;

namespace auralis {
class DsdSink;
namespace core {
class NavigatorImpl;
}
} // namespace auralis

namespace auralis::app {

/// --self-check --soak 的結果：用 Capture 後端以實際時間的裝置節奏連續播放測試訊號。
struct SoakResult {
    /// 實際播放的秒數。
    int seconds = 0;
    /// 開始聽到的曲目數（含第一首）。
    int tracks = 0;
    /// 換到不同格式（重開裝置）的次數。
    int formatChanges = 0;
    /// 同格式直接接續（不重開裝置）的次數。
    int sameFormatJoins = 0;
    /// 播放引擎的欠載次數（換格式時刻意排空的不算）。
    qint64 underruns = 0;
    /// 送進裝置的資料裡，PCM 相鄰樣本差超過門檻的次數與 DoP 標記沒交替的框數。
    qint64 pcmJumps = 0;
    qint64 dopBreaks = 0;
    /// PCM 相鄰樣本差的最大值與門檻（滿刻度的比例）。
    double maxPcmStep = 0.0;
    double stepThreshold = 0.0;
    /// 有要求 DSD 時（--soak-dsd）：是否真的經過 DSD 包裝器、包裝器名稱、DSD 曲目數，
    /// 沒有包裝器時略過的原因。
    bool dsdRequested = false;
    QString dsdSink;
    int dsdTracks = 0;
    QString dsdSkipped;
    /// 播放中途失敗的原因；沒有失敗時為空。
    QString error;
};

/// 最短的 soak 秒數：要涵蓋一次換格式與一次同格式接續。
constexpr int kMinSoakSeconds = 5;

/// 播放 seconds 秒（呼叫端的執行緒會等到播完）。不開實體裝置、不讀寫檔案。
/// withDsd 時測試訊號加入 DSD64 曲目，經 dsdSink（進階輸出模組註冊的包裝器）播放，
/// 和 PCM 交替；dsdSink 是 nullptr 時 DSD 部分略過，原因寫在 dsdSkipped。
/// stop 被設成 true 時不等滿秒數，盡快停下（--fullscreen 的主視窗提前結束時）。
SoakResult runSoak(int seconds, bool withDsd = false, std::shared_ptr<DsdSink> dsdSink = {},
                   const std::atomic_bool* stop = nullptr);

/// 要列進 self-check errors 的問題：欠載、不連續、播放失敗，以及沒涵蓋到換格式或同格式接續。
QStringList soakErrors(const SoakResult& result);

QJsonObject soakJson(const SoakResult& result);

/// --soak --fullscreen（AC-17-5 的驗證）：soak 期間開著主視窗的全螢幕頁。開好時回傳空字串；
/// 沒有登記全螢幕頁（歌詞模組停用）時回傳要列進 errors 的訊息，不靜默略過。只在 UI 執行緒呼叫。
QString openSoakFullscreen(core::NavigatorImpl& navigator);

/// --soak --fullscreen 期間 UI 是否撐到 soak 跑完：中途離開全螢幕或主視窗提前結束時，
/// 原因記在 interruptions（要列進 errors），不讓 AC-17-5 的驗證假陽性。
struct FullscreenSoakWatch {
    /// 主視窗提前結束時設成 true，要求 soak 執行緒盡快停。
    std::atomic_bool stop{false};
    /// 只在 UI 執行緒寫；soak 執行緒結束後才讀。
    QStringList interruptions;
    bool leftFullscreen = false;
    QMetaObject::Connection connection;
};

/// 開好全螢幕頁後開始監看：離開全螢幕（Esc、F11、開別的頁）時記一筆，只記一次。只在 UI 執行緒呼叫。
void watchSoakFullscreen(core::NavigatorImpl& navigator, FullscreenSoakWatch& watch);

/// 事件迴圈返回後呼叫：停止監看；soak 執行緒還沒跑完（主視窗先結束）時記一筆並要求它停，
/// 然後等它結束。只在 UI 執行緒呼叫。
void finishSoakAfterUi(QThread& soakThread, FullscreenSoakWatch& watch);

} // namespace auralis::app
