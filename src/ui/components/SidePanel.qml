pragma ComponentBehavior: Bound
import QtQuick

// 右側面板（介面規範 §4）：模組與核心登記的頁籤（正在播放、歌詞、佇列），Ctrl+I 開合。
Rectangle {
    id: panel

    property alias currentIndex: tabs.currentIndex
    readonly property var panels: UiRegistry.panels

    color: Theme.bg
    clip: true

    // 切到這個 id 的頁籤（Navigator.showPanel）；沒有這個頁籤時不動
    function select(panelId) {
        const index = panel.panels.findIndex(entry => entry.id === panelId)
        if (index >= 0)
            panel.currentIndex = index
    }

    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: Theme.hairline
        color: Theme.line
    }

    Tabs {
        id: tabs
        visible: panel.panels.length > 1
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.topMargin: Theme.space3
        // 佇列的頁籤帶曲目數（畫面規格 S-13：「佇列 <n>」）
        model: panel.panels.map(entry => entry.id === "panel/queue"
                                ? qsTr("佇列 %1").arg(Player.queue.count) : entry.title)
    }

    PageArea {
        anchors { left: parent.left; right: parent.right; top: tabs.visible ? tabs.bottom : parent.top; bottom: parent.bottom }
        anchors.leftMargin: Theme.hairline
        source: panel.panels.length > 0
                // 只有一個面板時頁籤不顯示，currentIndex 可能是 -1
                ? panel.panels[Math.max(0, Math.min(panel.currentIndex, panel.panels.length - 1))].source : ""
    }
}
