#pragma once

#include <QString>
#include <QVariantMap>

namespace auralis {

/// 開頁與返回。頁面 id 是 UiRegistry 登記的 id（介面規範的畫面清單）。
/// 只在 UI 執行緒呼叫。QML 的 Navigator 另有：showPanel(面板 id) 切右側面板的頁籤（收合時先展開，
/// 沒有登記時回傳 false、不做事）、enterFullscreen()／leaveFullscreen()／toggleFullscreen()
/// 進出主視窗的全螢幕頁、fullscreenPage（那一頁的 id，沒有時空字串）。
class Navigator {
public:
    virtual ~Navigator() = default;
    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;

    /// 開啟頁面並帶參數（例：{"albumId": 12}），回傳是否有這個頁面。目前頁進入返回的歷史。
    virtual bool open(const QString& pageId, const QVariantMap& params = {}) = 0;
    /// 回到上一頁；沒有上一頁時不做事並回傳 false。
    virtual bool back() = 0;
    virtual QString currentPage() const = 0;

protected:
    Navigator() = default;
};

} // namespace auralis
