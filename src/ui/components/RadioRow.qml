import QtQuick
import QtQuick.Templates as T

// 一列一個選項、右側有說明或狀態（介面規範 §3，例：輸出裝置）。列高 48。
// 選取：surfaceRaised 底＋實心圓點；不可用：停用並把 unavailableReason 寫在右側。
// 同一個父項目裡的 RadioRow 自動互斥；上下鍵移到上一個／下一個可用的列並選取。
// 使用者選了這一列（點擊或方向鍵）時發出 selected()。
T.RadioButton {
    id: control

    property string detail          // 第二欄（例：裝置名，inkMuted）
    property string note            // 右側說明（例：「位元完美」）
    property string noteTone: "muted" // muted、amber、signal
    property string unavailableReason
    signal selected()

    readonly property bool isRadioRow: true
    // 選取圓點的大小
    readonly property int dotSize: Theme.space3

    implicitWidth: Theme.panelWidth
    implicitHeight: Theme.space12
    leftPadding: Theme.space4 + dotSize + Theme.space3
    rightPadding: Theme.space4
    enabled: unavailableReason.length === 0
    activeFocusOnTab: checked || !hasCheckedSibling()
    focusPolicy: Qt.StrongFocus
    Accessible.name: detail.length > 0 ? text + " " + detail : text
    Accessible.description: unavailableReason.length > 0 ? unavailableReason : note

    onClicked: selected()
    Keys.onUpPressed: moveSelection(-1)
    Keys.onDownPressed: moveSelection(1)

    function siblings() {
        return parent ? Array.from(parent.children).filter(item => item.isRadioRow === true && item.visible) : [control]
    }
    function hasCheckedSibling() {
        return siblings().some(item => item.checked)
    }
    function moveSelection(step) {
        const rows = siblings()
        for (let i = rows.indexOf(control) + step; i >= 0 && i < rows.length; i += step) {
            if (rows[i].enabled) {
                // 先移焦點再選取：Qt 不允許把目前有焦點的列移出 Tab 順序
                rows[i].forceActiveFocus(Qt.TabFocusReason)
                rows[i].checked = true
                rows[i].selected()
                return
            }
        }
    }

    indicator: Rectangle {
        x: Theme.space4
        anchors.verticalCenter: parent.verticalCenter
        width: control.dotSize
        height: control.dotSize
        radius: Theme.radiusFull
        color: Theme.transparent
        border.width: Theme.hairline
        border.color: control.enabled ? Theme.ink : Theme.inkFaint

        Rectangle {
            visible: control.checked
            anchors.centerIn: parent
            width: Theme.space1 + Theme.space1 / 2
            height: width
            radius: Theme.radiusFull
            color: Theme.ink
        }
    }

    contentItem: Item {
        implicitHeight: Theme.lineBody
        Text {
            id: title
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.4)
            text: control.text
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
            elide: Text.ElideRight
        }
        Text {
            anchors.left: title.right
            anchors.leftMargin: Theme.space3
            anchors.right: noteText.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: control.detail
            font: Theme.body
            color: control.enabled ? Theme.inkMuted : Theme.inkFaint
            elide: Text.ElideRight
        }
        Text {
            id: noteText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.unavailableReason.length > 0 ? control.unavailableReason : control.note
            font: Theme.small
            color: control.unavailableReason.length > 0 ? Theme.inkMuted
                   : control.noteTone === "amber" ? Theme.amber
                   : control.noteTone === "signal" ? Theme.signal
                   : Theme.inkMuted
        }
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.checked || control.hovered ? Theme.surfaceRaised : Theme.transparent

        Behavior on color {
            ColorAnimation { duration: Theme.motionState; easing.type: Easing.InOutQuad }
        }

        FocusFrame {
            anchors.margins: Theme.focusWidth
            visible: control.visualFocus
        }
    }
}
