pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Auralis.Ui
import Auralis.Pl

// 側欄的播放清單區（畫面規格 S-02 側欄、S-08 側欄項目；US-11 AC-1、3、4）：
// 標籤「播放清單」＋「新增播放清單」；每個播放清單一列（點了開 playlist 頁）；
// 新增時那一列直接變成可編輯的名稱（F2 也一樣），Enter 確定、Esc 取消；
// 右鍵、選單鍵或 Shift+F10 開「播放」（T-55）「重新命名」「刪除播放清單」，Delete 鍵＝刪除（先確認）。
// 拖放：曲目拖到某一列＝加到那個播放清單；有曲目拖進這一區時，最下方出現
// 「拖到這裡建立新播放清單」的虛線框，拖到框裡＝建立新播放清單並加入。
Item {
    id: section

    implicitHeight: column.implicitHeight

    /// 這一區裡有幾個接收區正被拖曳經過（外框、各列、虛線框，進出時各自加減）
    property int dragsInside: 0
    // 從外框移到某一列或虛線框時是先離開再進入，中間有一下是 0：晚一點才收起虛線框，不然它一收起
    // 就拖不進去了
    readonly property bool trackDragging: dragsInside > 0 || hideDelay.running
    /// 清單重讀後要把焦點放回去的那一列（改名確定後；重讀會重建每一列）
    property var refocusId: 0

    function dragChanged(inside) {
        dragsInside = Math.max(0, dragsInside + (inside ? 1 : -1))
        if (dragsInside === 0)
            hideDelay.restart()
    }

    function rowFor(id) {
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.itemAt(i)
            if (row && row.objectName === "playlist-" + id)
                return row
        }
        return null
    }

    function restoreFocus() {
        const row = rowFor(refocusId)
        refocusId = 0
        if (row)
            row.sidebarItem.forceActiveFocus()
    }

    Timer {
        id: hideDelay
        interval: Theme.motionPanel
    }

    Connections {
        target: Playlists
        function onPlaylistsChanged() {
            if (section.refocusId !== 0)
                Qt.callLater(section.restoreFocus)
        }
    }

    // 放下的處理晚一點做：加入後清單重讀會重建每一列，不能在那一列的 dropped 訊號裡重建它
    property var pendingDrop: null

    function dropLater(id, json) {
        pendingDrop = { id: id, json: json }
        Qt.callLater(section.flushDrop)
    }

    function flushDrop() {
        const drop = pendingDrop
        pendingDrop = null
        if (!drop)
            return
        if (drop.id === 0)
            Playlists.createWithTracks(drop.json)
        else
            Playlists.addTracks(drop.id, drop.json)
    }

    Column {
        id: column
        width: parent.width

        Item {
            width: column.width
            height: Theme.rowHeightCompact

            // 標籤列只用來知道「有曲目拖進這一區」，放下時不接受。不用一個蓋住整區的接收區：
            // Qt 的拖曳會留在先進入的接收區，蓋在各列外面的話各列就收不到
            DropArea {
                anchors.fill: parent
                keys: [TrackDragCodec.mimeType]
                onContainsDragChanged: section.dragChanged(containsDrag)
                onDropped: drop => drop.accepted = false
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("播放清單")
                font: Theme.label
                color: Theme.inkMuted
            }
            IconButton {
                objectName: "addPlaylistButton"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                iconName: "add"
                text: qsTr("新增播放清單")
                onClicked: Playlists.createPlaylist()
            }
        }

        Repeater {
            id: rows
            model: Playlists.playlists
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool editing: Playlists.editingId === row.modelData.id
                readonly property alias sidebarItem: item
                readonly property alias nameField: nameField

                objectName: "playlist-" + row.modelData.id
                width: column.width
                height: editing ? nameField.implicitHeight + Theme.space2 : item.implicitHeight

                SidebarItem {
                    id: item
                    anchors.fill: parent
                    visible: !row.editing
                    text: row.modelData.name
                    count: String(row.modelData.count)
                    current: Navigator.currentPage === "playlist"
                             && Number(Navigator.params.id) === row.modelData.id
                    dropTarget: drop.containsDrag
                    onClicked: Navigator.open("playlist", { id: row.modelData.id })

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_F2) {
                            Playlists.editingId = row.modelData.id
                            event.accepted = true
                        } else if (event.key === Qt.Key_Delete) {
                            Playlists.requestRemove(row.modelData.id)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Menu
                                   || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
                            itemMenu.openFor(row.modelData.id, item, true)
                            event.accepted = true
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: itemMenu.openFor(row.modelData.id, item, false)
                    }
                }

                TrackDropArea {
                    id: drop
                    anchors.fill: parent
                    enabled: !row.editing
                    onContainsDragChanged: section.dragChanged(containsDrag)
                    onTracksDropped: payload => section.dropLater(row.modelData.id, payload.json)
                }

                TextField {
                    id: nameField
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.space3
                    anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.editing
                    label: qsTr("播放清單名稱")
                    onVisibleChanged: {
                        if (visible) {
                            text = row.modelData.name
                            input.selectAll()
                            input.forceActiveFocus()
                        }
                    }
                    onAccepted: commit()
                    input.onActiveFocusChanged: {
                        if (!input.activeFocus && row.editing)
                            commit()
                    }
                    Keys.onEscapePressed: event => {
                        Playlists.editingId = 0
                        item.forceActiveFocus()
                        event.accepted = true
                    }

                    function commit() {
                        // 用 Enter 確定時焦點回到這一列（重讀清單會重建每一列，所以等重讀後再放）；
                        // 空白名稱不改，照原本的名稱結束編輯
                        const id = row.modelData.id
                        const byKey = input.activeFocus
                        if (byKey)
                            section.refocusId = id
                        if (!Playlists.rename(id, text)) {
                            section.refocusId = 0
                            Playlists.editingId = 0
                            if (byKey)
                                item.forceActiveFocus()
                        }
                    }
                }
            }
        }

        // 「拖到這裡建立新播放清單」：只在有曲目拖進這一區時出現（介面規範 §4）
        Item {
            id: newZone
            objectName: "newPlaylistZone"
            visible: section.trackDragging
            width: column.width
            height: visible ? Theme.rowHeight + Theme.space2 : 0

            Shape {
                anchors.fill: parent
                anchors.margins: Theme.space1
                anchors.leftMargin: Theme.space3
                anchors.rightMargin: Theme.space3
                ShapePath {
                    strokeColor: newDrop.containsDrag ? Theme.ink : Theme.lineStrong
                    strokeWidth: Theme.hairline
                    strokeStyle: ShapePath.DashLine
                    fillColor: newDrop.containsDrag ? Theme.surfaceRaised : Theme.transparent
                    startX: 0; startY: 0
                    PathLine { x: newZone.width - Theme.space3 * 2; y: 0 }
                    PathLine { x: newZone.width - Theme.space3 * 2; y: newZone.height - Theme.space1 * 2 }
                    PathLine { x: 0; y: newZone.height - Theme.space1 * 2 }
                    PathLine { x: 0; y: 0 }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.space3 * 2 - Theme.space2 * 2
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("拖到這裡建立新播放清單")
                font: Theme.small
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            TrackDropArea {
                id: newDrop
                anchors.fill: parent
                onContainsDragChanged: section.dragChanged(containsDrag)
                onTracksDropped: payload => section.dropLater(0, payload.json)
            }
        }

        // 「匯入播放清單」（S-09，T-51）：開檔對話框，可多選。畫面規格寫在側欄底部，
        // 但核心的側欄底部沒有給模組的位置，先放在播放清單區的最後
        SidebarItem {
            objectName: "importPlaylistItem"
            width: column.width
            text: qsTr("匯入播放清單")
            onClicked: PlaylistImport.chooseFiles()
        }
    }

    // 匯入結果（S-09）：側欄一直都在，對話框放這裡
    PlaylistImportDialog {}
    // 匯出（S-10，T-54）：播放清單頁的「匯出」與下面右鍵選單的「匯出」都開這一個
    PlaylistExportDialog {}

    // 側欄項目的右鍵選單；playlistId 是開選單時的那一列
    Menu {
        id: itemMenu
        objectName: "playlistItemMenu"
        property var playlistId: 0
        property bool byKeyboard: false

        function openFor(id, target, keyboard) {
            playlistId = id
            byKeyboard = keyboard
            if (keyboard)
                popup(target, 0, target.height)
            else
                popup()
        }
        onOpened: {
            // 鍵盤開的時候停在第一個能用的項目（空的播放清單不能播放）
            if (byKeyboard)
                currentIndex = playItem.enabled ? 0 : 1
        }

        MenuItem {
            id: playItem
            text: qsTr("播放")
            enabled: (Playlists.info(itemMenu.playlistId).count ?? 0) > 0
            onTriggered: Playlists.play(itemMenu.playlistId)
        }
        MenuItem {
            text: qsTr("重新命名")
            shortcut: "F2"
            onTriggered: Playlists.editingId = itemMenu.playlistId
        }
        MenuItem {
            text: qsTr("匯出")
            onTriggered: PlaylistExport.open(itemMenu.playlistId)
        }
        MenuItem {
            text: qsTr("刪除播放清單")
            shortcut: "Delete"
            onTriggered: Playlists.requestRemove(itemMenu.playlistId)
        }
    }
}
