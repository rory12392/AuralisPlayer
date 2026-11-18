import QtQuick
import Auralis.Ui
import Auralis.Lib

// 曲目右鍵選單裡本模組的區塊（S-07，T-25；UiRegistry.addTrackMenuSection，order 50）：
// 「前往專輯」「前往藝人」，只有一首、而且找得到它的專輯時可用。
// 前往專輯＝library/album {albumId}；前往藝人＝library/artists {artist}（專輯的顯示藝人，
// 藝人頁以它分組）。區塊每次打開選單都重建，做的事交給 Navigator，不留在區塊上。
TrackMenuGroup {
    id: group

    readonly property var location: LibraryAlbums.trackLocation(group.payloadJson)
    readonly property bool found: (group.location.albumId ?? 0) > 0

    MenuItem {
        text: qsTr("前往專輯")
        enabled: group.found
        onTriggered: Navigator.open("library/album", { albumId: group.location.albumId })
    }
    MenuItem {
        text: qsTr("前往藝人")
        enabled: group.found && (group.location.artist ?? "").length > 0
        onTriggered: Navigator.open("library/artists", { artist: group.location.artist })
    }
}
