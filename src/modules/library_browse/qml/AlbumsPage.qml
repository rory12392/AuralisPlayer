pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib

// 專輯（畫面規格 S-03，US-10 AC-1、2、5）：頁面工具列的檢視方式（封面牆／清單／詳細，存 LIB/albumsView）
// 與排序；篩選列沿用 LibraryFilterBar（右側「<n> 張專輯」，和搜尋框以外的篩選疊加）。
// 資料是 LibraryAlbums（整個音樂庫的專輯，掃描完成自動重讀；掃描中每 3 秒重讀一次，新專輯逐步出現，
// 已顯示的列不重建、不跳回頂端）。雙擊或 Enter 開專輯詳細（library/album，T-24 登記；
// 還沒有時不做事），封面 hover 的播放鍵播放整張專輯。右鍵選單與拖放是 T-23。
Item {
    id: page
    objectName: "libraryAlbumsPage"

    readonly property bool failed: LibraryAlbums.error.length > 0 && !LibraryAlbums.loaded
    readonly property bool loading: !failed && !LibraryAlbums.loaded
    readonly property bool empty: LibraryAlbums.loaded && LibraryAlbums.total === 0
    readonly property bool noMatch: LibraryAlbums.loaded && LibraryAlbums.total > 0
                                    && LibraryAlbums.count === 0
    readonly property string countText: LibraryAlbums.loaded
        ? qsTr("%1 張專輯").arg(Number(LibraryAlbums.count).toLocaleString(Qt.locale(), "f", 0)) : ""
    // 主視窗的狀態列（PageArea）
    property string statusText: countText
    // 狀態列左側的選取說明（T-25，目前的檢視以 Binding 填入）
    property string statusTextLeft

    readonly property var views: ["grid", "list", "detail"]
    readonly property var sorts: ["added", "title", "artist", "year"]

    // 頁面工具列（AppShell 放在搜尋框右邊）：檢視方式與排序
    property Component toolbar: Row {
        id: toolbarRow
        objectName: "albumsToolbar"
        spacing: Theme.space2

        SegmentedControl {
            id: viewControl
            objectName: "albumsView"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("檢視方式")
            model: [{ text: qsTr("封面牆") }, { text: qsTr("清單") }, { text: qsTr("詳細") }]
            currentIndex: Math.max(0, page.views.indexOf(LibraryAlbums.view))
            onActivated: index => LibraryAlbums.view = page.views[index]
        }

        Select {
            id: sortSelect
            objectName: "albumsSort"
            anchors.verticalCenter: parent.verticalCenter
            // 剩下的寬度（工具列的寬度由 AppShell 的 Loader 給）
            readonly property real room: toolbarRow.width > 0
                                         ? toolbarRow.width - viewControl.width - toolbarRow.spacing : Infinity
            // 960 寬時放不下「排序」標籤＋值：只顯示值（標籤仍是無障礙名稱），文字才不會壓到箭頭（M-02 U-1）
            readonly property bool compact: room < leftPadding + rightPadding + sortLabelMetrics.advanceWidth
                                                   + Theme.space2 + sortValueMetrics.advanceWidth
            width: Math.max(Theme.space24, Math.min(implicitWidth, room))
            label: compact ? "" : qsTr("排序")
            Accessible.name: qsTr("排序")
            model: [qsTr("加入時間"), qsTr("專輯名"), qsTr("專輯藝人"), qsTr("年份")]
            currentIndex: Math.max(0, page.sorts.indexOf(LibraryAlbums.sort))
            onActivated: index => LibraryAlbums.sort = page.sorts[index]

            TextMetrics {
                id: sortLabelMetrics
                font: Theme.small
                text: qsTr("排序")
            }
            TextMetrics {
                id: sortValueMetrics
                font: Theme.body
                text: sortSelect.displayText
            }
        }
    }

    function openAlbum(row) {
        const id = LibraryAlbums.albumIdAt(row)
        // 專輯詳細頁由 T-24 登記；還沒有時 open 回傳 false，不做事
        if (id > 0)
            Navigator.open("library/album", { albumId: id })
    }

    Component.onCompleted: LibraryAlbums.load()

    // 掃描中定時重讀（LibraryQuery 只在掃描完成時通知），新專輯逐步出現
    Timer {
        interval: 3000
        repeat: true
        running: Library.scanning && page.visible
        onTriggered: LibraryAlbums.reload()
    }

    LibraryFilterBar {
        id: filterBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.filterBarHeight
        countText: page.countText
    }

    Loader {
        id: viewLoader
        objectName: "albumsViewLoader"
        visible: LibraryAlbums.loaded && LibraryAlbums.count > 0
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        sourceComponent: LibraryAlbums.view === "list" ? listView
                         : LibraryAlbums.view === "detail" ? detailView : gridView
    }

    Component {
        id: gridView
        AlbumGrid {
            id: albumGrid
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumGrid.selectionText }
        }
    }
    Component {
        id: listView
        AlbumList {
            id: albumList
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumList.selectionText }
        }
    }
    Component {
        id: detailView
        AlbumDetailList {
            id: albumDetail
            onOpenRequested: row => page.openAlbum(row)
            Binding { target: page; property: "statusTextLeft"; value: albumDetail.selectionText }
        }
    }

    // 載入中（超過 150 毫秒才出現，Skeleton 自己處理）：封面牆是封面方塊，其他是列
    Flow {
        objectName: "albumsLoading"
        visible: page.loading
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; margins: Theme.space6 }
        spacing: Theme.space4
        Repeater {
            model: 6
            delegate: Skeleton {
                variant: LibraryAlbums.view === "grid" ? "cover" : "row"
                size: Theme.space24 + Theme.space12
                width: variant === "row" ? (parent ? parent.width : 0) : size
            }
        }
    }

    EmptyState {
        objectName: "albumsEmpty"
        visible: page.empty
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        title: qsTr("音樂庫裡還沒有專輯")
        description: qsTr("加入音樂資料夾後，專輯會出現在這裡。")
        actionText: qsTr("加入音樂資料夾")
        onAction: Library.chooseFolder()
    }

    EmptyState {
        objectName: "albumsNoMatch"
        visible: page.noMatch
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        title: qsTr("沒有符合的專輯")
        actionText: qsTr("清除篩選")
        onAction: LibrarySearch.clearFilters()
    }

    ErrorBlock {
        objectName: "albumsError"
        visible: page.failed
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        message: qsTr("無法載入專輯。")
        onRetry: LibraryAlbums.reload()
    }
}
