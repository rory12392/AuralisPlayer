import QtQuick

// 頁面或面板載入失敗（介面規範 §6）：說明＋「重試」，其他區域照常運作。
Item {
    id: block

    property string message: qsTr("這個區塊無法載入。")
    signal retry()

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.centerIn: parent
        spacing: Theme.space4
        width: Math.min(block.width, Theme.panelWidth)

        Text {
            width: parent.width
            text: block.message
            font: Theme.body
            color: Theme.ink
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("重試")
            onClicked: block.retry()
        }
    }
}
