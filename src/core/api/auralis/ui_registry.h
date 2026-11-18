#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace auralis {

/// 模組接手的檔案（例：PL 匯入播放清單）。收到的是完整本機路徑清單，在 UI 執行緒呼叫；
/// 核心不讀檔內容，要讀檔的工作由模組自己丟到工作執行緒。
using FileOpenHandler = std::function<void(const QStringList& paths)>;

/// 模組放進主視窗的一塊介面：頁面、側欄區塊、面板頁籤、設定區塊…
/// source 是 QML 檔的網址（通常是模組自己 QML 模組裡的 qrc:/qt/qml/...）；id 是頁面 id，
/// 規則見介面規範的畫面清單（例：library/albums）；order 小的排前面。
struct UiContribution {
    QString id;
    /// 給使用者看的名稱（側欄項目、頁籤、設定分區標題），呼叫端用 tr() 翻好再傳入。
    QString title;
    QUrl source;
    int order = 0;
};

/// 註冊模組的介面。只在 UI 執行緒、模組 start() 期間呼叫；登記之後主視窗立刻反映。
/// 每一塊都由核心用 Loader 載入：載入失敗只讓那一塊顯示錯誤區塊（附重試），其他區域照常。
/// 同一種類裡重複的 id 以後登記的取代先登記的。
class UiRegistry {
public:
    virtual ~UiRegistry() = default;
    UiRegistry(const UiRegistry&) = delete;
    UiRegistry& operator=(const UiRegistry&) = delete;

    /// 側欄「音樂庫」區的頁面（例：專輯、藝人），也可以用 Navigator 開。
    virtual void addSidebarPage(const UiContribution& page) = 0;
    /// 不在側欄的頁面（例：專輯詳細、播放清單），只用 Navigator 開。
    virtual void addPage(const UiContribution& page) = 0;
    /// 側欄裡的一整個區塊（例：播放清單區）。
    virtual void addSidebarSection(const UiContribution& section) = 0;
    /// 右側面板的頁籤（例：歌詞）。
    virtual void addPanel(const UiContribution& panel) = 0;
    /// 全螢幕頁（例：全螢幕正在播放），用 Navigator 開。order 最小的那一頁是主視窗的全螢幕頁：
    /// F11、點正在播放面板的封面進入，F11、Esc 離開回到原頁（Esc 是頁面自己沒用掉時才離開）。
    virtual void addFullscreenPage(const UiContribution& page) = 0;
    /// 設定頁的一個分區。
    virtual void addSettingsSection(const UiContribution& section) = 0;
    /// 輸出面板的一個區塊（例：DSD 輸出、ReplayGain）。
    virtual void addOutputPanelSection(const UiContribution& section) = 0;
    /// 曲目右鍵選單的一段（例：「加入播放清單 ▸」）；元件收到的是被選取曲目的 TrackDragPayload。
    virtual void addTrackMenuSection(const UiContribution& section) = 0;
    /// 主視窗工具列的常駐項目（例：搜尋框），在頁面自己的工具列左邊、
    /// 每一頁都看得到（全螢幕時不顯示）。項目元件用 implicitWidth 決定自己的寬度；
    /// 載入失敗時那一塊不出現（寫日誌）。title 不用、可以空白。
    virtual void addToolbarItem(const UiContribution& item) = 0;
    /// 正在播放面板的區塊（例：LYR 的歌詞預覽），在四格讀數下方的髮絲線之後，依 order 由上往下。
    /// 區塊檔的根是一般的 Item，寬度由面板決定、高度用 implicitHeight；
    /// 要切到別的面板頁籤時呼叫 QML 的 Navigator.showPanel。
    /// 沒有登記時面板不留空位；載入失敗時那一塊不出現（寫日誌）。title 不用、可以空白。
    virtual void addNowPlayingSection(const UiContribution& section) = 0;
    /// 拖進主視窗、命令列、另一個執行個體轉來的檔案中，副檔名在 extensions 裡的（小寫、不含點；
    /// 比對不分大小寫）交給 handler，不加入佇列；同一批一個 handler 呼叫一次。
    /// 拖進來的資料夾裡的檔案不分流（照舊只找音樂檔）。同一個副檔名以後登記的取代先登記的。
    virtual void addFileOpenHandler(const QStringList& extensions, FileOpenHandler handler) = 0;

protected:
    UiRegistry() = default;
};

} // namespace auralis
