pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib

// 所有曲目與搜尋結果（畫面規格 S-07 的基本版，US-09）：篩選列、表頭、虛擬化的曲目清單。
// 資料是 LibrarySearch 的結果（條件全空＝全部曲目）；雙擊或 Enter 從這首開始播放（LibraryPlayback）。
// 狀態：載入（Skeleton row）、空音樂庫、搜尋或篩選無結果（規格 §1 情況 5）、錯誤（ErrorBlock＋重試）。
// 表頭與清單在 LibraryTrackList（T-23 抽出來給資料夾頁共用）。
// 選取、拖放與右鍵選單在 LibraryTrackList（T-25）；全選超過 5,000 首時只帶搜尋條件。
// 頁面工具列的「欄位」與表頭拖曳欄寬（T-90）：存 LIB/trackColumns，和資料夾頁共用。
Item {
    id: page
    objectName: "libraryTracksPage"

    readonly property bool narrowed: LibrarySearch.text.length > 0 || LibrarySearch.filtersActive
    readonly property bool failed: LibrarySearch.error.length > 0
    readonly property bool loading: !failed && LibrarySearch.busy && LibrarySearch.count === 0
    readonly property bool empty: !failed && !LibrarySearch.busy && LibrarySearch.complete
                                  && LibrarySearch.count === 0
    // 「<n> 首符合」只在全部載入後才是總數；之前顯示已載入的筆數（「已載入 <n> 首」）
    readonly property string countText: {
        if (failed || (LibrarySearch.busy && LibrarySearch.count === 0))
            return ""
        const n = Number(LibrarySearch.count).toLocaleString(Qt.locale(), "f", 0)
        if (!LibrarySearch.complete)
            return qsTr("已載入 %1 首").arg(n)
        return narrowed ? qsTr("%1 首符合").arg(n) : qsTr("%1 首").arg(n)
    }
    // 主視窗的狀態列（PageArea）
    property string statusText: countText
    property string statusTextLeft: list.visible ? list.selectionText : ""

    // 頁面工具列（AppShell 放在搜尋框右邊）：「欄位」選單（T-90）
    // （工具列的 Loader 會把項目撐滿剩下的寬度，所以包一層 Row，按鈕維持自己的大小）
    property Component toolbar: Row {
        TrackColumnsButton {
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    function playRow(row) {
        if (row >= 0)
            LibraryPlayback.playFrom(row)
    }

    // 第一次顯示：還沒查過（或上次沒結果）就用目前條件查
    Component.onCompleted: {
        if (LibrarySearch.count === 0 && !LibrarySearch.busy)
            LibrarySearch.refresh()
    }

    LibraryFilterBar {
        id: filterBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.filterBarHeight
        countText: page.countText
    }

    // 表頭＋清單（LibraryTrackList，資料夾頁共用）
    LibraryTrackList {
        id: list
        listName: "libraryTrackList"
        visible: !page.failed && !page.empty && !page.loading
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        model: LibrarySearch.results
        allQuery: list.selection.searchQuery(LibrarySearch.text, LibrarySearch.artist, LibrarySearch.album,
                                             LibrarySearch.year, LibrarySearch.format, LibrarySearch.sampleRate)
        complete: LibrarySearch.complete
        onPlayRequested: row => page.playRow(row)

        // 新查詢的結果回來時回到頂端（同一個查詢分批接上不動）
        Connections {
            target: LibrarySearch
            function onResultsReady() {
                list.toTop()
            }
        }
    }

    // 載入中（超過 150 毫秒才出現，Skeleton 自己處理）
    Column {
        objectName: "libraryTracksLoading"
        visible: page.loading
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom }
        anchors.topMargin: Theme.rowHeightCompact
        Repeater {
            model: 6
            delegate: Skeleton {
                width: parent ? parent.width : 0
                variant: "row"
            }
        }
    }

    // 空音樂庫
    EmptyState {
        objectName: "libraryTracksEmpty"
        visible: page.empty && !page.narrowed
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        title: qsTr("音樂庫裡還沒有曲目")
        description: qsTr("加入音樂資料夾後，曲目會出現在這裡。")
        actionText: qsTr("加入音樂資料夾")
        onAction: Library.chooseFolder()
    }

    // 搜尋或篩選無結果（模組規格 §1 情況 5）：有搜尋字串時一起清掉，只有篩選時只清篩選
    EmptyState {
        objectName: "libraryTracksNoMatch"
        visible: page.empty && page.narrowed
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        title: qsTr("沒有符合的曲目")
        actionText: LibrarySearch.text.length > 0 ? qsTr("清除搜尋與篩選") : qsTr("清除篩選")
        onAction: {
            if (LibrarySearch.text.length > 0)
                LibrarySearch.clear()
            else
                LibrarySearch.clearFilters()
        }
    }

    ErrorBlock {
        objectName: "libraryTracksError"
        visible: page.failed
        anchors { left: parent.left; right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
        message: qsTr("無法載入曲目。")
        onRetry: LibrarySearch.refresh()
    }
}
