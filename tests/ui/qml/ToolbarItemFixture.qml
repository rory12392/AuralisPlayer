import QtQuick
import Auralis.Ui

// 工具列常駐項目（tst_toolbar，T-28）：外觀像搜尋框、寬度由 implicitWidth 決定。不接焦點，
// 免得改到其他放了主視窗殼的測試（tst_output、tst_playback…）的 Tab 順序。
Rectangle {
    objectName: "fixtureToolbarSearch"
    implicitWidth: Theme.panelWidth
    implicitHeight: Theme.controlHeight
    color: Theme.surface
    border.width: Theme.hairline
    border.color: Theme.line

    Icon {
        id: icon
        anchors.left: parent.left
        anchors.leftMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        tone: "muted"
    }
    Text {
        anchors.left: icon.right
        anchors.leftMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        text: "搜尋曲名、藝人、專輯"
        font: Theme.body
        color: Theme.inkMuted
    }
}
