import QtQuick

// 通知列（介面規範 §3、§6）：Notifier 的畫面，在播放列正上方。一般通知 6 秒後自動收起，
// 錯誤要手動關；可帶一個動作按鈕。不搶走焦點。
Rectangle {
    id: bar

    readonly property var notice: Notifier.current
    readonly property bool shown: notice.id !== undefined
    readonly property bool isError: notice.kind === "error"

    height: shown ? Theme.rowHeight : 0
    visible: shown
    color: Theme.surface
    Accessible.role: Accessible.AlertMessage
    Accessible.name: shown ? notice.text : ""

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.hairline
        color: bar.isError ? Theme.signal : Theme.line
    }

    Timer {
        interval: Theme.noticeTimeout
        running: bar.shown && !bar.isError
        onTriggered: Notifier.dismiss(bar.notice.id)
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2

        Text {
            visible: bar.isError
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("錯誤")
            font: Theme.label
            color: Theme.signal
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.shown ? bar.notice.text : ""
            font: Theme.body
            color: Theme.ink
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2

        Button {
            visible: bar.shown && bar.notice.actionText.length > 0
            text: bar.shown ? bar.notice.actionText : ""
            focusPolicy: Qt.TabFocus
            onClicked: Notifier.triggerAction(bar.notice.id)
        }
        Button {
            text: qsTr("關閉")
            focusPolicy: Qt.TabFocus
            onClicked: Notifier.dismiss(bar.notice.id)
        }
    }
}
