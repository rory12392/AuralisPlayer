import QtQuick
import Auralis.Ui

// 曲目右鍵選單的「計算 ReplayGain」（T-88，AC-06-3、畫面規格 S-07）：區塊介面照核心 UiRegistry 的曲目選單區塊規則
// 「T-26 定稿」。觸發後交給單例 R128 在背景算（選單關閉後這個物件就銷毀）。
// 計算中停用（同時只跑一個）；只帶查詢條件（超過 5,000 首）時停用：R128 只認得曲目清單，
// 選取那麼多時用設定頁的「計算整個音樂庫的 ReplayGain」。
MenuItem {
    id: item

    required property string payloadJson
    readonly property var decoded: TrackDragCodec.decode(payloadJson)
    readonly property bool hasTracks: decoded.valid && decoded.tracks.length > 0

    objectName: "r128TrackMenuItem"
    text: qsTr("計算 ReplayGain")
    enabled: item.hasTracks && !R128.running
    onTriggered: R128.computeTracks(item.payloadJson)
}
