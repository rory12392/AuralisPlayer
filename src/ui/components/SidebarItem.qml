import QtQuick
import QtQuick.Templates as T

// 側欄項目（介面規範 §3）：目前頁是 surfaceRaised 底；右側可放數量（等寬）；
// 拖曳目標（dropTarget，例：曲目拖到播放清單上）是 surfaceRaised 底＋1px ink 框。
T.ItemDelegate {
    id: control

    property bool current: false
    property string count: ""
    property bool dropTarget: false

    implicitWidth: Theme.sidebarWidth
    implicitHeight: Theme.rowHeightCompact
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: count.length > 0 ? text + " " + count : text
    Accessible.role: Accessible.PageTab

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.right: countText.left
            anchors.rightMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: Theme.body
            color: Theme.ink
            elide: Text.ElideRight
        }
        Text {
            id: countText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.count
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.current || control.down || control.hovered || control.dropTarget
               ? Theme.surfaceRaised : Theme.transparent
        border.width: control.dropTarget ? Theme.hairline : 0
        border.color: Theme.ink
        Behavior on color {
            ColorAnimation { duration: Theme.motionState; easing.type: Easing.InOutQuad }
        }
        FocusFrame {
            anchors.margins: Theme.focusWidth
            visible: control.visualFocus
        }
    }
}
