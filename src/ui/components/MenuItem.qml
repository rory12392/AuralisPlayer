import QtQuick
import QtQuick.Templates as T

// 選單項目（介面規範 §3）：列高 36；右側可放快捷鍵（等寬小字）；有子選單時右側是箭頭。
T.MenuItem {
    id: control

    property string shortcut

    implicitWidth: Math.max(Theme.sidebarWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.rowHeightCompact
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    Accessible.name: text

    contentItem: Item {
        implicitWidth: label.implicitWidth + Theme.space6 + trailing.implicitWidth
        Text {
            id: label
            anchors.left: parent.left
            anchors.right: trailing.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
            elide: Text.ElideRight
        }
        Item {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: control.subMenu ? arrow.width : shortcutText.implicitWidth
            height: parent.height
            Text {
                id: shortcutText
                visible: !control.subMenu
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: control.shortcut
                font: Theme.monoSmall
                color: control.enabled ? Theme.inkMuted : Theme.inkFaint
            }
            Icon {
                id: arrow
                visible: control.subMenu !== null
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"
                size: 16
                tone: control.enabled ? "ink" : "faint"
            }
        }
    }

    background: Rectangle {
        color: control.highlighted && control.enabled ? Theme.surfaceRaised : Theme.transparent
        radius: Theme.radius0
    }
}
