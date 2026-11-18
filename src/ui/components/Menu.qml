import QtQuick
import QtQuick.Templates as T

// 右鍵選單、下拉內容（介面規範 §3）：surface 底、1px ink 框，沒有陰影。項目用共用元件 MenuItem。
// 方向鍵移動、Enter 執行、Esc 關閉、關閉後焦點回到原本的元件（Popup 本身處理）。
// QtQuick.Templates 的 Menu 沒有內建 contentItem，沒有它時按方向鍵會當掉，所以這裡一定要給。
T.Menu {
    id: control

    implicitWidth: Math.max(Theme.sidebarWidth, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: Theme.hairline
    margins: 0
    overlap: Theme.hairline
    focus: true

    delegate: MenuItem {}

    contentItem: ListView {
        id: list
        implicitWidth: contentWidth
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll { flickable: list; lineHeight: Theme.rowHeightCompact }
    }

    background: Rectangle {
        implicitWidth: Theme.sidebarWidth
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink
        radius: Theme.radius0
    }
}
