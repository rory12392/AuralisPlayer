import QtQuick

// 載入中的佔位（介面規範 §3、§6）：surfaceRaised 色塊，不做閃光動畫。
// variant：row（與清單列同高：縮圖＋兩段文字）、cover（正方形，邊長 size）。
// 載入超過 150 毫秒才顯示，避免一閃而過（delayed 設 false 時立即顯示）。
Item {
    id: skeleton

    property string variant: "row"
    property int size: Theme.space24
    property bool delayed: true

    // 介面規範 §6：清單載入超過 150 毫秒才顯示佔位
    readonly property int showDelay: 150
    property bool shown: !delayed

    implicitWidth: variant === "cover" ? size : Theme.panelWidth
    implicitHeight: variant === "cover" ? size : Theme.rowHeight
    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("載入中")

    Timer {
        interval: skeleton.showDelay
        running: !skeleton.shown && skeleton.visible
        onTriggered: skeleton.shown = true
    }

    Rectangle {
        visible: skeleton.shown && skeleton.variant === "cover"
        anchors.fill: parent
        color: Theme.surfaceRaised
        radius: Theme.radius0
    }

    Row {
        visible: skeleton.shown && skeleton.variant === "row"
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        spacing: Theme.space3

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.coverThumb
            height: Theme.coverThumb
            color: Theme.surfaceRaised
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.35
            height: Theme.space3
            color: Theme.surfaceRaised
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.2
            height: Theme.space3
            color: Theme.surfaceRaised
        }
    }
}
