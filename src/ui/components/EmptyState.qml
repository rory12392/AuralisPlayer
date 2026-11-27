import QtQuick

// 空資料（介面規範 §3、§6）：標題＋說明＋最多一個按鈕（下一步，例：「加入音樂資料夾」）。
// actionText 空字串時不顯示按鈕；按下發出 action()。
Item {
    id: emptyState

    property string title
    property string description
    property string actionText
    signal action()

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(emptyState.width, Theme.panelWidth)
        spacing: Theme.space3

        Text {
            width: parent.width
            text: emptyState.title
            font: Theme.heading
            color: Theme.ink
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            visible: emptyState.description.length > 0
            width: parent.width
            text: emptyState.description
            font: Theme.body
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Item {
            visible: emptyState.actionText.length > 0
            width: parent.width
            height: visible ? actionButton.height + Theme.space3 : 0
            Button {
                id: actionButton
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                text: emptyState.actionText
                onClicked: emptyState.action()
            }
        }
    }
}
