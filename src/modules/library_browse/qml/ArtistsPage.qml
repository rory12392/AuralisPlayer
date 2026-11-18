pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib

// 藝人（畫面規格 S-05，US-10 AC-3）：左欄藝人清單（寬 280、列高 rowHeightCompact、右側專輯數，
// 頂端「全部藝人」），右邊是所選藝人的專輯，用專輯頁目前的檢視方式（LibraryAlbums.view）顯示。
// 以專輯藝人分組，沒有專輯藝人時用第一首的藝人，「Various Artists」不拆開（LibraryArtists）。
// 資料和專輯頁共用（LibraryAlbums 讀好的專輯），掃描中每 3 秒重讀。
// 狀態：載入（Skeleton）、空音樂庫、錯誤（ErrorBlock＋重試）。右邊專輯的選取、右鍵選單與拖放在三種檢視裡（T-25）；
// 開頁參數 {artist} 選中該藝人（T-25）。
Item {
    id: page
    objectName: "libraryArtistsPage"

    readonly property bool failed: LibraryArtists.error.length > 0 && !LibraryArtists.loaded
    readonly property bool loading: !failed && !LibraryArtists.loaded
    readonly property bool empty: LibraryArtists.loaded && LibraryArtists.artistCount === 0
    readonly property string countText: LibraryArtists.loaded && !empty
        ? qsTr("%1 張專輯").arg(Number(LibraryArtists.count).toLocaleString(Qt.locale(), "f", 0)) : ""
    // 主視窗的狀態列（PageArea）
    property string statusText: countText
    // 狀態列左側的選取說明（T-25，目前的檢視以 Binding 填入）
    property string statusTextLeft

    // 開頁參數 {artist}（T-25；右鍵「前往藝人」、核心 T-27 的正在播放面板）：選中該藝人，
    // 找不到時「全部藝人」。離開這一頁時 Navigator.params 會先換成下一頁的，所以只在這一頁是目前頁時跟著換
    readonly property var artistParam: Navigator.currentPage === "library/artists" ? Navigator.params.artist : undefined
    onArtistParamChanged: {
        if (artistParam !== undefined)
            LibraryArtists.selectArtist(String(artistParam))
    }

    // S-05 的左欄寬（沒有對應的 token，見模組規格整合待辦）
    readonly property int paneWidth: 280

    function openAlbum(row) {
        const id = LibraryArtists.albumIdAt(row)
        // 專輯詳細頁由 T-24 登記；還沒有時 open 回傳 false，不做事
        if (id > 0)
            Navigator.open("library/album", { albumId: id })
    }

    Component.onCompleted: {
        LibraryArtists.load()
        if (artistParam !== undefined)
            LibraryArtists.selectArtist(String(artistParam))
    }

    // 掃描中定時重讀（同專輯頁），新藝人與專輯逐步出現
    Timer {
        interval: 3000
        repeat: true
        running: Library.scanning && page.visible
        onTriggered: LibraryArtists.reload()
    }

    // ---- 左欄：藝人清單 ----
    ListView {
        id: artistList
        objectName: "artistList"
        visible: LibraryArtists.loaded && !page.empty
        width: page.paneWidth
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        clip: true
        model: LibraryArtists.artists
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        reuseItems: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("藝人")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: artistList; lineHeight: Theme.rowHeightCompact }

        // 上下鍵移動就換右邊的專輯
        onCurrentIndexChanged: {
            if (currentIndex >= 0 && currentIndex !== LibraryArtists.selectedRow)
                LibraryArtists.select(currentIndex)
        }
        // 選取由 LibraryArtists 記住（重讀後列號可能變）：跟著它
        Component.onCompleted: currentIndex = LibraryArtists.selectedRow
        Connections {
            target: LibraryArtists
            function onSelectionChanged() {
                if (artistList.currentIndex !== LibraryArtists.selectedRow)
                    artistList.currentIndex = LibraryArtists.selectedRow
            }
        }

        delegate: SidebarItem {
            id: artistRow
            required property int index
            required property string name
            required property int albumCount
            required property bool all

            width: ListView.view.width
            // 鍵盤焦點在清單本身，列不另外取得焦點
            activeFocusOnTab: false
            focusPolicy: Qt.NoFocus
            current: ListView.isCurrentItem
            text: artistRow.all ? qsTr("全部藝人")
                  : artistRow.name.length > 0 ? artistRow.name : qsTr("未知藝人")
            count: Number(artistRow.albumCount).toLocaleString(Qt.locale(), "f", 0)
            Accessible.role: Accessible.ListItem
            onClicked: {
                LibraryArtists.select(artistRow.index)
                artistList.forceActiveFocus()
            }
        }
    }

    // 左右欄的分隔線
    Rectangle {
        id: divider
        visible: artistList.visible
        anchors { left: artistList.right; top: parent.top; bottom: parent.bottom }
        width: Theme.hairline
        color: Theme.line
    }

    // ---- 右邊：所選藝人的專輯（專輯頁目前的檢視方式）----
    Loader {
        id: viewLoader
        objectName: "artistAlbumsLoader"
        visible: artistList.visible
        anchors { left: divider.right; right: parent.right; top: parent.top; bottom: parent.bottom }
        sourceComponent: LibraryAlbums.view === "list" ? listView
                         : LibraryAlbums.view === "detail" ? detailView : gridView
    }

    Component {
        id: gridView
        AlbumGrid {
            id: albumGrid
            source: LibraryArtists
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumGrid.selectionText }
        }
    }
    Component {
        id: listView
        AlbumList {
            id: albumList
            source: LibraryArtists
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumList.selectionText }
        }
    }
    Component {
        id: detailView
        AlbumDetailList {
            id: albumDetail
            source: LibraryArtists
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumDetail.selectionText }
        }
    }

    // 載入中（超過 150 毫秒才出現，Skeleton 自己處理）：左欄是列
    Column {
        objectName: "artistsLoading"
        visible: page.loading
        width: page.paneWidth
        anchors { left: parent.left; top: parent.top }
        Repeater {
            model: 8
            delegate: Skeleton {
                width: parent ? parent.width : 0
                variant: "row"
            }
        }
    }

    EmptyState {
        objectName: "artistsEmpty"
        visible: page.empty
        anchors.fill: parent
        title: qsTr("音樂庫裡還沒有藝人")
        description: qsTr("加入音樂資料夾後，藝人會出現在這裡。")
        actionText: qsTr("加入音樂資料夾")
        onAction: Library.chooseFolder()
    }

    ErrorBlock {
        objectName: "artistsError"
        visible: page.failed
        anchors.fill: parent
        message: qsTr("無法載入藝人。")
        onRetry: LibraryArtists.reload()
    }
}
