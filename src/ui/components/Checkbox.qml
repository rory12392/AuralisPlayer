import QtQuick
import QtQuick.Templates as T

// 開關類設定（介面規範 §3）：14px 方框，勾選時填 ink；不做膠囊形開關。
// text 是標籤（同時是 Accessible.name），description 是下方的說明。
T.CheckBox {
    id: control

    property string description

    // 方框邊長（介面規範 §3 指定 14px）
    readonly property int boxSize: 14

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.hitMin, implicitContentHeight)
    leftPadding: boxSize + Theme.space2
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.description: description

    indicator: Rectangle {
        x: 0
        // 和標籤第一行垂直置中
        y: control.topPadding + Math.round((Theme.lineBody - height) / 2)
        width: control.boxSize
        height: control.boxSize
        radius: Theme.radius0
        color: control.checked ? (control.enabled ? Theme.ink : Theme.inkFaint) : Theme.transparent
        border.width: Theme.hairline
        border.color: control.enabled ? Theme.ink : Theme.inkFaint

        FocusFrame {
            visible: control.visualFocus
        }
    }

    contentItem: Column {
        spacing: Theme.space1
        Text {
            text: control.text
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
        }
        Text {
            visible: control.description.length > 0
            text: control.description
            font: Theme.small
            color: control.enabled ? Theme.inkMuted : Theme.inkFaint
        }
    }
}
