import QtQuick

// 檔案拖進主視窗時的提示（AC-11-5）：整個視窗畫 1px ink 框（拖曳目標，和 TrackDropArea 的樣子一致），
// 中間一塊說明放開會做什麼。只是畫面，不接收事件；主視窗把它疊在殼的上面。
Item {
    id: hint

    visible: false

    Rectangle {
        anchors.fill: parent
        color: Theme.transparent
        border.width: Theme.hairline
        border.color: Theme.ink
    }

    Rectangle {
        objectName: "fileDropHint"
        anchors.centerIn: parent
        width: label.implicitWidth + 2 * Theme.space6
        height: label.implicitHeight + 2 * Theme.space4
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink

        Text {
            id: label
            anchors.centerIn: parent
            text: qsTr("放開即加到佇列最後並開始播放")
            font: Theme.body
            color: Theme.ink
        }
    }
}
