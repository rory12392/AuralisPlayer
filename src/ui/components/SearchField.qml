import QtQuick
import QtQuick.Templates as T

// 搜尋框（介面規範 §3）：左側搜尋圖示、有字時右側出現清除鈕與 amber 底線；Ctrl+F 聚焦、Esc 清除。
// 文字改變時發出 textChanged（頁面自己去抖）。placeholderText 同時是 Accessible.name。
T.TextField {
    id: control

    // 同一個視窗只有一個搜尋框接 Ctrl+F（工具列那個）
    property bool findShortcut: true

    implicitWidth: Theme.panelWidth
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space2 + 16 + Theme.space2
    rightPadding: clearButton.visible ? clearButton.width + Theme.space1 : Theme.space2
    font: Theme.body
    color: Theme.ink
    placeholderTextColor: Theme.inkMuted
    selectionColor: Theme.ink
    selectedTextColor: Theme.bg
    verticalAlignment: TextInput.AlignVCenter
    activeFocusOnTab: true
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    Keys.onEscapePressed: event => {
        if (text.length > 0) {
            clear()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    Shortcut {
        enabled: control.findShortcut && control.visible && control.enabled
        sequences: [StandardKey.Find]
        onActivated: {
            control.forceActiveFocus(Qt.ShortcutFocusReason)
            control.selectAll()
        }
    }

    Icon {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: 16
        tone: "muted"
    }

    IconButton {
        id: clearButton
        visible: control.text.length > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.space1
        anchors.verticalCenter: parent.verticalCenter
        iconName: "clear"
        text: qsTr("清除")
        // 鍵盤用 Esc 清除，Tab 不停在這裡
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        onClicked: {
            control.clear()
            control.forceActiveFocus()
        }
    }

    background: Rectangle {
        radius: Theme.radius0
        color: Theme.transparent
        border.width: Theme.hairline
        border.color: control.activeFocus ? Theme.ink : Theme.lineStrong

        // Templates 的 TextField 不畫提示文字，由這裡畫；框窄時（最小視窗）截斷在框內
        Text {
            objectName: "searchPlaceholder"
            visible: control.length === 0 && control.preeditText.length === 0
            x: control.leftPadding
            width: Math.max(0, control.width - control.leftPadding - control.rightPadding)
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            text: control.placeholderText
            font: Theme.body
            color: Theme.inkMuted
        }

        // 輸入焦點與有字（正在套用搜尋，S-07）用 amber 底線表示（琥珀的四個用途之一）
        Rectangle {
            objectName: "searchUnderline"
            visible: control.activeFocus || control.text.length > 0
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2 * Theme.hairline
            color: Theme.amber
        }
    }
}
