import QtQuick
import Auralis.Ui
import Auralis.Pl

// 播放清單頁頂部（畫面規格 S-08）：名稱（textHeading，F2 或點兩下原地改名，Enter 確定、Esc 取消）、
// 等寬「<n> 首　<總長>」、「播放」（solid，從第一首播整個清單）、更多選單（「重新命名」「刪除播放清單」，
// 刪除先確認）、「匯出」（outline，開 S-10 對話框，T-54）。
Item {
    id: header

    property var playlistId
    property string name
    readonly property bool editing: nameField.visible
    /// 改名結束後焦點要回去的地方（頁面給清單）
    property Item returnFocus: null

    implicitHeight: content.implicitHeight

    function startRename() {
        nameField.text = header.name
        nameField.visible = true
        nameField.input.selectAll()
        nameField.input.forceActiveFocus()
    }

    function finishRename(commit) {
        if (!nameField.visible)
            return
        nameField.visible = false
        if (commit)
            Playlists.rename(Number(header.playlistId), nameField.text)
        if (header.returnFocus)
            header.returnFocus.forceActiveFocus()
    }

    Row {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.space4

        Column {
            width: content.width - actions.width - content.spacing
            spacing: Theme.space2

            Text {
                id: title
                objectName: "playlistName"
                width: parent.width
                visible: !header.editing
                text: header.name
                font: Theme.heading
                color: Theme.ink
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text

                TapHandler {
                    onDoubleTapped: header.startRename()
                }
            }
            TextField {
                id: nameField
                objectName: "playlistNameField"
                width: parent.width
                visible: false
                label: qsTr("播放清單名稱")
                onAccepted: header.finishRename(true)
                input.onActiveFocusChanged: {
                    if (!input.activeFocus)
                        header.finishRename(true)
                }
                Keys.onEscapePressed: event => {
                    header.finishRename(false)
                    event.accepted = true
                }
            }
            Text {
                objectName: "playlistSummary"
                text: qsTr("%1 首　%2")
                      .arg(Number(PlaylistTracks.count).toLocaleString(Qt.locale(), "f", 0))
                      .arg(PlaylistTracks.totalDurationText)
                font: Theme.monoSmall
                color: Theme.inkMuted
            }
        }

        Row {
            id: actions
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2

            Button {
                objectName: "playPlaylistButton"
                variant: "solid"
                text: qsTr("播放")
                enabled: PlaylistTracks.count > 0
                onClicked: PlaylistTracks.playAll()
            }
            Button {
                objectName: "exportPlaylistButton"
                variant: "outline"
                text: qsTr("匯出")
                onClicked: PlaylistExport.open(Number(header.playlistId))
            }
            IconButton {
                id: moreButton
                objectName: "playlistMoreButton"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "more"
                text: qsTr("更多")
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            }
        }
    }

    Menu {
        id: moreMenu
        objectName: "playlistMoreMenu"
        MenuItem {
            text: qsTr("重新命名")
            shortcut: "F2"
            // 選單關閉時會把焦點還給「更多」，晚一點才進入改名，不然輸入框一拿到焦點就被搶走
            onTriggered: Qt.callLater(header.startRename)
        }
        MenuItem {
            text: qsTr("刪除播放清單")
            onTriggered: Playlists.requestRemove(Number(header.playlistId))
        }
    }
}
