import QtQuick
import Auralis.Ui
import Auralis.Pl

// 播放清單頁（畫面規格 S-08，頁面 id playlist，開頁參數 {id}；US-11 AC-1）：頂部名稱與動作
// （PlaylistHeader），下方曲目清單（PlaylistTrackList）。狀態：載入（Skeleton，超過 150 毫秒才出現）、
// 空（EmptyState）、有資料、錯誤（ErrorBlock＋重試）。播放清單被刪掉時回到上一頁。
// 空的狀態下也能把曲目拖進來（接收區在清單那一層，蓋住空狀態）。
Item {
    id: page

    // 開頁參數；離開這一頁時 Navigator.params 會先換成下一頁的，所以只在這一頁是目前頁時跟著換
    readonly property var playlistId: Navigator.currentPage === "playlist" ? Navigator.params.id : undefined
    property var info: null
    readonly property string status: PlaylistTracks.status

    function refresh() {
        if (playlistId !== undefined) {
            info = Playlists.info(Number(playlistId))
            PlaylistTracks.playlistId = Number(playlistId)
        }
    }

    onPlaylistIdChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: Playlists
        function onPlaylistsChanged() { page.info = Playlists.info(Number(page.playlistId)) }
        function onPlaylistRemoved(id) {
            if (Number(page.playlistId) === id)
                Navigator.back()
        }
    }

    // F2 在頁面任何地方（清單有焦點時也是）都是重新命名這個播放清單
    Keys.onPressed: event => {
        if (event.key === Qt.Key_F2 && !header.editing) {
            header.startRename()
            event.accepted = true
        }
    }

    PlaylistHeader {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.space6
        visible: page.info?.exists ?? false
        playlistId: page.playlistId
        name: page.info?.name ?? ""
        returnFocus: tracks.list
    }

    // 有找不到的檔案時頂部一行（S-08，T-52）：「<m> 首找不到檔案」＋「重新對應資料夾」（開 S-09 的對應區）
    Item {
        id: missingBar
        objectName: "playlistMissingBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        anchors.topMargin: visible ? Theme.space4 : 0
        visible: page.status === "ready" && PlaylistTracks.missingCount > 0 && (page.info?.exists ?? false)
        height: visible ? remapButton.implicitHeight : 0

        Text {
            objectName: "playlistMissingText"
            anchors.left: parent.left
            anchors.right: remapButton.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 首找不到檔案").arg(
                      Number(PlaylistTracks.missingCount).toLocaleString(Qt.locale("en_US"), "f", 0))
            font: Theme.body
            color: Theme.inkMuted
            elide: Text.ElideRight
        }
        Button {
            id: remapButton
            objectName: "playlistRemapButton"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("重新對應資料夾")
            onClicked: PlaylistImport.openRemap(Number(page.playlistId))
        }
    }

    PlaylistTrackList {
        id: tracks
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: missingBar.bottom
        anchors.topMargin: Theme.space4
        anchors.bottom: parent.bottom
        visible: page.info?.exists ?? false
        // 載入中與錯誤時不顯示清單（空的時候清單仍在，接住拖進來的曲目）
        opacity: page.status === "ready" ? 1 : 0
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tracks.top
        anchors.topMargin: Theme.rowHeight
        visible: page.status === "loading"
        Repeater {
            model: 6
            Skeleton {
                width: parent.width
                variant: "row"
            }
        }
    }

    EmptyState {
        objectName: "playlistEmpty"
        anchors.centerIn: tracks
        visible: page.status === "ready" && PlaylistTracks.count === 0 && (page.info?.exists ?? false)
        title: qsTr("這個播放清單還沒有曲目")
        description: qsTr("從音樂庫把曲目拖進來，或匯入 foobar2000 的播放清單。")
        actionText: qsTr("匯入播放清單")
        onAction: PlaylistImport.chooseFiles()
    }

    ErrorBlock {
        objectName: "playlistError"
        anchors.centerIn: tracks
        visible: page.status === "error"
        message: qsTr("無法讀取這個播放清單的曲目。")
        onRetry: PlaylistTracks.reload()
    }
}
