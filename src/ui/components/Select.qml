pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 下拉選擇（介面規範 §3，例：排序、取樣率）。label 顯示在值的左邊（inkMuted），同時是 Accessible.name。
// 鍵盤：Tab 停在這裡，上下鍵直接換值，空白鍵或 Enter 開清單，Esc 關閉。選了之後發出 activated(index)。
T.ComboBox {
    id: control

    property string label

    implicitWidth: Math.max(Theme.space24 + Theme.space12, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space3
    rightPadding: Theme.space2 + 16 + Theme.space2
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: label

    delegate: T.ItemDelegate {
        id: item
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Theme.rowHeightCompact
        leftPadding: Theme.space3
        rightPadding: Theme.space3
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        Accessible.name: text

        contentItem: Text {
            text: item.text
            font: control.currentIndex === item.index ? Theme.bodyMedium : Theme.body
            color: Theme.ink
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: item.highlighted ? Theme.surfaceRaised : Theme.transparent
            radius: Theme.radius0
        }
    }

    // 值的寬度跟著內容區：Select 被限制寬度時值在箭頭前截斷，不壓到箭頭
    contentItem: Item {
        implicitWidth: valueText.x + valueText.implicitWidth
        implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)
        Text {
            id: labelText
            visible: control.label.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: control.label
            font: Theme.small
            color: Theme.inkMuted
        }
        Text {
            id: valueText
            objectName: "selectValue"
            x: labelText.visible ? labelText.implicitWidth + Theme.space2 : 0
            width: Math.max(0, parent.width - x)
            anchors.verticalCenter: parent.verticalCenter
            text: control.displayText
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
            elide: Text.ElideRight
        }
    }

    indicator: Icon {
        x: control.width - width - Theme.space2
        y: (control.height - height) / 2
        name: control.popup.visible ? "chevron-up" : "chevron-down"
        size: 16
        tone: control.enabled ? "ink" : "faint"
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.down || control.hovered || control.popup.visible ? Theme.surfaceRaised : Theme.transparent
        border.width: Theme.hairline
        border.color: !control.enabled ? Theme.line : control.popup.visible ? Theme.ink : Theme.lineStrong

        FocusFrame {
            visible: control.visualFocus
        }
    }

    popup: T.Popup {
        y: control.height + Theme.space1
        width: Math.max(control.width, Theme.sidebarWidth)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                                 Theme.rowHeightCompact * 10)
        padding: Theme.hairline

        contentItem: ListView {
            id: options
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll { flickable: options; lineHeight: Theme.rowHeightCompact }
        }

        background: Rectangle {
            color: Theme.surface
            border.width: Theme.hairline
            border.color: Theme.ink
            radius: Theme.radius0
        }
    }
}
