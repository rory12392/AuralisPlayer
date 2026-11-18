pragma ComponentBehavior: Bound
import QtQuick

// 帶四個角標的髮絲線框（介面規範 §3）。title 是左上的標籤；index 在右上，只放有資訊的東西
// （快捷鍵、數量），沒有就留空。內容放在 Panel 裡面，由內容的 implicitHeight 決定高度。無互動。
Item {
    id: panel

    property string title
    property string index
    default property alias content: body.data

    // 角標的長度
    readonly property int cornerLength: Theme.space2

    implicitWidth: Theme.panelWidth
    implicitHeight: header.height + body.childrenRect.height + 2 * Theme.space6

    Rectangle {
        anchors.fill: parent
        color: Theme.transparent
        border.width: Theme.hairline
        border.color: Theme.line
        radius: Theme.radius0
    }

    // 四個角：ink 的 L 形短線，壓在 line 色的框上
    Repeater {
        model: 4
        delegate: Item {
            id: corner
            required property int index
            readonly property bool atRight: index === 1 || index === 3
            readonly property bool atBottom: index >= 2
            x: atRight ? panel.width - panel.cornerLength : 0
            y: atBottom ? panel.height - panel.cornerLength : 0
            width: panel.cornerLength
            height: panel.cornerLength
            Rectangle {
                y: corner.atBottom ? corner.height - height : 0
                width: corner.width
                height: Theme.hairline
                color: Theme.ink
            }
            Rectangle {
                x: corner.atRight ? corner.width - width : 0
                width: Theme.hairline
                height: corner.height
                color: Theme.ink
            }
        }
    }

    Item {
        id: header
        visible: panel.title.length > 0 || panel.index.length > 0
        x: Theme.space6
        y: Theme.space6
        width: panel.width - 2 * Theme.space6
        height: visible ? Theme.lineLabel + Theme.space4 : 0

        Text {
            anchors.left: parent.left
            anchors.right: indexText.left
            anchors.rightMargin: Theme.space3
            text: panel.title
            font: Theme.label
            color: Theme.inkMuted
            elide: Text.ElideRight
        }
        Text {
            id: indexText
            anchors.right: parent.right
            text: panel.index
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
    }

    Item {
        id: body
        x: Theme.space6
        y: header.y + header.height
        width: panel.width - 2 * Theme.space6
        height: childrenRect.height
    }
}
