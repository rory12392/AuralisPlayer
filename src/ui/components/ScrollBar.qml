import QtQuick
import QtQuick.Templates as T

// 捲軸（介面規範 §3）：寬 8，平常是 1px lineStrong 的把手線，hover 或拖曳時加寬成整條。
// 用法：ListView { ScrollBar.vertical: ScrollBar {} }，或設 orientation 後自己放。
T.ScrollBar {
    id: control

    implicitWidth: orientation === Qt.Vertical ? Theme.space2 : Theme.sidebarWidth
    implicitHeight: orientation === Qt.Vertical ? Theme.sidebarWidth : Theme.space2
    padding: 0
    minimumSize: Theme.space8 / Math.max(1, orientation === Qt.Vertical ? height : width)
    visible: policy === T.ScrollBar.AlwaysOn || size < 1
    Accessible.role: Accessible.ScrollBar

    contentItem: Item {
        readonly property bool wide: control.hovered || control.pressed

        Rectangle {
            anchors.right: control.orientation === Qt.Vertical ? parent.right : undefined
            anchors.bottom: control.orientation === Qt.Horizontal ? parent.bottom : undefined
            width: control.orientation === Qt.Vertical ? (parent.wide ? parent.width : Theme.hairline) : parent.width
            height: control.orientation === Qt.Horizontal ? (parent.wide ? parent.height : Theme.hairline) : parent.height
            color: Theme.lineStrong
            radius: Theme.radius0
        }
    }
}
