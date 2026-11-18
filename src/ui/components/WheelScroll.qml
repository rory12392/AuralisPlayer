import QtQuick

// 滾輪捲動（介面規範 §3）：一格滾輪（angleDelta 120）捲「Windows 一次捲動的行數」（預設 3）× lineHeight，
// 系統設成一次捲動一個畫面時捲一個可見高度；觸控板的平滑捲動（pixelDelta）照實際距離，不放大。
// 用法：放在要捲的 Flickable／ListView／GridView 裡面：ListView { id: list; WheelScroll { flickable: list } }，
// 列高不是 Theme.rowHeight 時設 lineHeight。它蓋在清單上但不收滑鼠按鍵，點擊、拖放與 hover 照常；
// 到頂或到底、或是橫向的滾輪不接走，外層的捲動區照常捲。
MouseArea {
    id: area

    required property Flickable flickable
    // 一行的高度：清單用列高，設定頁這類一般內容用預設值
    property real lineHeight: Theme.rowHeight

    parent: flickable
    anchors.fill: parent
    // 在清單內容（contentItem）之上，滾輪先到這裡
    z: 1
    acceptedButtons: Qt.NoButton
    onWheel: wheel => area.scroll(wheel)

    // 依 wheel（WheelEvent，或測試用的 { angleDelta, pixelDelta, accepted }）捲動，回傳實際捲動的距離。
    function scroll(wheel) {
        const view = area.flickable
        let delta = 0
        if (wheel.pixelDelta.y !== 0) {
            delta = wheel.pixelDelta.y
        } else if (Math.abs(wheel.angleDelta.y) >= Math.abs(wheel.angleDelta.x)) {
            const lines = Application.styleHints.wheelScrollLines
            const step = lines < 0 ? view.height : lines * area.lineHeight
            delta = wheel.angleDelta.y / 120 * step
        }
        const top = view.originY - view.topMargin
        const bottom = Math.max(top, view.originY + view.contentHeight + view.bottomMargin - view.height)
        const target = Math.min(bottom, Math.max(top, view.contentY - delta))
        if (delta === 0 || target === view.contentY) {
            wheel.accepted = false
            return 0
        }
        const moved = target - view.contentY
        view.cancelFlick()
        // 和內建的滾輪捲動一樣發出移動開始與結束，靠它們做事的頁面（清掉待捲到的分區、捲完載入）照常
        view.movementStarted()
        view.contentY = target
        view.movementEnded()
        return moved
    }
}
