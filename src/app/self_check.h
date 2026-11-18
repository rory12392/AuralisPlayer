#pragma once

#include "modules/module_manager.h"
#include "plugins/plugin_manager.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QQuickWindow;

namespace auralis::app {

/// --self-check 的結果。errors 為空才算通過（結束碼 0）。
struct SelfCheckReport {
    QString version;
    QString dataDir;
    bool devBuild = false;
    /// 升級後的資料庫版本（PRAGMA user_version）；升級失敗時為 0。
    int schemaVersion = 0;
    std::vector<core::ModuleStatus> modules;
    /// 外掛清單與狀態（T-103）；外掛不符或載入失敗不算錯誤（程式照常運作，設定頁顯示原因）。
    QList<core::PluginStatus> plugins;
    QStringList errors;
    /// --probe-output 時才有：每個輸出路徑的裝置與獨占支援的格式。
    std::optional<QJsonArray> outputs;
    /// --soak 時才有：長時間播放的秒數、曲目數、換格式次數與欠載次數。
    std::optional<QJsonObject> soak;
    /// --bench-albumwall 時才有：封面牆捲動的平均、最低與第 5 百分位 fps（AC-10-2）。
    std::optional<QJsonObject> albumWall;
};

/// 封面牆捲動量測的門檻（AC-10-2：快速捲動 ≥ 50 fps）。
constexpr double kAlbumWallMinFps = 50.0;

/// 開發建置的 --self-check --bench-albumwall：在 window 裡找 objectName 為 albumGrid 的 GridView
/// （LIB 的 library/albums 封面牆），等專輯數穩定後每一幀把 contentY 往下推半列、捲到底再回頂，
/// 記每兩次 frameSwapped 的間隔。做完（或找不到、逾時）時以結果呼叫 done（失敗時有 error 欄位）。
void startAlbumWallBench(QQuickWindow* window, std::function<void(QJsonObject)> done);

/// 量測結果沒過門檻或量不到時的錯誤訊息（放進 errors）。
QStringList albumWallErrors(const QJsonObject& result);

/// 一行的中文摘要（寫到 stderr，stdout 只放 JSON）。
QString albumWallSummary(const QJsonObject& result);

/// 執行中的 FFmpeg 版本與建置設定（T-98，STACK §10 的授權與 --disable-network 檢查照它判斷）：
/// version、configuration（avcodec_configuration() 原文）、license（avcodec_license()），
/// 以及從建置設定判斷的 gpl、nonfree、networkDisabled。
QJsonObject ffmpegBuildInfo();

/// 啟動失敗的模組也算錯誤：自我檢查要讓問題浮出來，即使一般啟動時程式會照常運作。
void collectModuleErrors(SelfCheckReport& report);

/// 唯讀查詢輸出裝置（列舉、IsFormatSupported），不初始化、不發聲；查詢失敗寫進該項的 error。
QJsonArray probeOutputs(const core::OutputRegistryImpl& registry);

QByteArray toJson(const SelfCheckReport& report);

} // namespace auralis::app
