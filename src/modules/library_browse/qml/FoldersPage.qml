pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib

// 資料夾（畫面規格 S-06，US-10 AC-3）：左欄資料夾樹（預設寬 320，拖曳分隔線調整、存 LIB/foldersPaneWidth），
// 根是使用者加入的音樂資料夾，結構與磁碟一致（folders 表）；右邊是所選資料夾（含子資料夾）的曲目，
// 用 S-07 的清單（LibraryTrackList），上方是所選資料夾的路徑（等寬）與曲目數。
// 樹的鍵盤：上下移動（選取跟著換）、右展開（已展開時移到第一個子資料夾）、左收合（已收合時移到父資料夾）、
// Enter 切換展開（空白鍵留給全域的播放／暫停）。清單：上下鍵、Enter 或雙擊從這首播放（佇列是右邊整份曲目）。
// 狀態：載入、空（沒有音樂資料夾）、有資料、資料夾已不存在（列標「找不到資料夾」，右邊說明）、錯誤。
// T-25：右邊清單的選取、拖放與右鍵選單在 LibraryTrackList；樹的一列也能拖、能開右鍵選單（滑鼠右鍵、
// 選單鍵、Shift+F10），內容是整個資料夾（含子資料夾）＝右邊清單全選，超過 5,000 首時只帶資料夾條件。
Item {
    id: page
    objectName: "libraryFoldersPage"

    // 樹上整個資料夾的動作：等那個資料夾的曲目讀好才做（""、"select"、"menu"、"menuKeyboard"）
    property string pendingFolderAction

    // 頁面工具列：右邊曲目清單的「欄位」選單（T-90，和所有曲目頁共用 LIB/trackColumns）
    // （包一層 Row：工具列的 Loader 會把項目撐滿剩下的寬度）
    property Component toolbar: Row {
        TrackColumnsButton {
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    function folderAction(row, action) {
        LibraryFolders.select(row)
        if (LibraryFolders.tracksLoaded)
            runFolderAction(action)
        else
            pendingFolderAction = action
    }

    function runFolderAction(action) {
        pendingFolderAction = ""
        if (LibraryFolders.trackCount === 0)
            return
        trackList.selection.selectAll()
        const json = trackList.selection.payloadJson()
        if (action === "menu")
            folderMenu.popupAt(json, trackList.selection.trackCount)
        else if (action === "menuKeyboard")
            folderMenu.popupFrom(json, trackList.selection.trackCount, treeList.currentItem ?? treeList)
    }

    Connections {
        target: LibraryFolders
        function onTracksChanged() {
            if (page.pendingFolderAction.length > 0 && LibraryFolders.tracksLoaded)
                page.runFolderAction(page.pendingFolderAction)
        }
    }

    TrackMenu {
        id: folderMenu
    }

    readonly property bool failed: LibraryFolders.error.length > 0 && !LibraryFolders.loaded
    readonly property bool loading: !failed && !LibraryFolders.loaded
    readonly property string countText: LibraryFolders.tracksLoaded && LibraryFolders.tracksError.length === 0
        ? qsTr("%1 首").arg(Number(LibraryFolders.trackCount).toLocaleString(Qt.locale(), "f", 0)) : ""
    // 主視窗的狀態列（PageArea）
    property string statusText: countText
    property string statusTextLeft: trackList.visible ? trackList.selectionText : ""

    Component.onCompleted: LibraryFolders.load()

    // 掃描中定時重讀（同專輯頁），新資料夾逐步出現
    Timer {
        interval: 3000
        repeat: true
        running: Library.scanning && page.visible
        onTriggered: LibraryFolders.reload()
    }

    // ---- 左欄：資料夾樹 ----
    ListView {
        id: treeList
        objectName: "folderTree"
        visible: LibraryFolders.loaded && count > 0
        // 視窗窄時左欄最多占頁面的四成，右邊的清單才放得下（設定值不變）
        width: Math.min(LibraryFolders.paneWidth, Math.round(page.width * 0.4))
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        clip: true
        model: LibraryFolders.tree
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        Accessible.role: Accessible.Tree
        Accessible.name: qsTr("資料夾")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: treeList; lineHeight: Theme.rowHeightCompact }

        function rowExpanded(row) {
            const item = itemAtIndex(row)
            return item !== null && (item as FolderTreeRow).expanded
        }
        function rowHasChildren(row) {
            const item = itemAtIndex(row)
            return item !== null && (item as FolderTreeRow).hasChildren
        }

        // 上下鍵移動就換右邊的曲目
        onCurrentIndexChanged: {
            if (currentIndex >= 0 && currentIndex !== LibraryFolders.selectedRow)
                LibraryFolders.select(currentIndex)
        }
        Keys.onRightPressed: {
            if (!LibraryFolders.expand(currentIndex) && rowExpanded(currentIndex) && rowHasChildren(currentIndex))
                currentIndex = currentIndex + 1 // 已展開：移到第一個子資料夾
        }
        Keys.onLeftPressed: {
            if (!LibraryFolders.collapse(currentIndex)) {
                const parentRow = LibraryFolders.parentRow(currentIndex)
                if (parentRow >= 0)
                    currentIndex = parentRow // 已收合或沒有子資料夾：移到父資料夾
            }
        }
        function toggle(row) {
            if (!LibraryFolders.collapse(row))
                LibraryFolders.expand(row)
        }
        Keys.onPressed: event => {
            if (folderMenu.isOpenKey(event) && currentIndex >= 0) {
                page.folderAction(currentIndex, "menuKeyboard")
                event.accepted = true
            }
        }
        Keys.onReturnPressed: toggle(currentIndex)
        Keys.onEnterPressed: toggle(currentIndex)

        // 選取由 LibraryFolders 記住（展開、收合、重讀後列號會變）：跟著它
        Component.onCompleted: currentIndex = LibraryFolders.selectedRow
        Connections {
            target: LibraryFolders
            function onSelectionChanged() {
                if (treeList.currentIndex !== LibraryFolders.selectedRow)
                    treeList.currentIndex = LibraryFolders.selectedRow
            }
        }

        delegate: FolderTreeRow {
            id: folderRow
            required property int index
            required property string name
            // depth、hasChildren、expanded、missing、root、path 是 FolderTreeRow 的 required property

            width: ListView.view.width
            text: folderRow.name
            current: ListView.isCurrentItem
            onClicked: {
                LibraryFolders.select(folderRow.index)
                treeList.forceActiveFocus()
            }
            onDoubleClicked: treeList.toggle(folderRow.index)
            onToggleRequested: treeList.toggle(folderRow.index)
            onMenuRequested: {
                treeList.forceActiveFocus()
                page.folderAction(folderRow.index, "menu")
            }

            // 拖放：整個資料夾（含子資料夾）。拖曳開始時選這個資料夾、曲目讀好後全選右邊清單
            readonly property bool selectedFolder: folderRow.index === LibraryFolders.selectedRow
            count: Math.max(1, folderRow.selectedFolder && LibraryFolders.tracksLoaded ? LibraryFolders.trackCount : 1)
            query: folderRow.dragging ? trackList.selection.query() : ""
            tracks: folderRow.dragging && trackList.selection.revision >= 0 && !trackList.selection.queryOnly
                    ? trackList.selection.tracks() : []
            onDraggingChanged: {
                if (folderRow.dragging)
                    page.folderAction(folderRow.index, "select")
            }

            // 鍵盤焦點：清單有焦點時目前列畫 2px 焦點框
            FocusFrame {
                visible: folderRow.ListView.isCurrentItem && treeList.activeFocus
            }
        }
    }

    // 分隔線＋拖曳調整左欄寬度（MouseArea 只用在拖曳，介面規範 §3 元件 API 規則）
    Rectangle {
        id: divider
        visible: treeList.visible
        anchors { left: treeList.right; top: parent.top; bottom: parent.bottom }
        width: Theme.hairline
        color: dragArea.containsMouse || dragArea.pressed ? Theme.lineStrong : Theme.line
    }
    MouseArea {
        id: dragArea
        objectName: "folderPaneHandle"
        visible: divider.visible
        anchors { horizontalCenter: divider.horizontalCenter; top: parent.top; bottom: parent.bottom }
        width: Theme.space2
        z: 1 // 蓋在右邊的清單上面（把手的右半邊和清單重疊）
        hoverEnabled: true
        // 右半邊蓋在曲目列上：拖曳中不讓列的拖放（TrackDragArea）搶走（T-25）
        preventStealing: true
        cursorShape: Qt.SplitHCursor
        property real startX: 0
        property int startWidth: 0
        onPressed: mouse => {
            startX = mapToItem(page, mouse.x, 0).x
            startWidth = treeList.width // 畫面上的寬度（可能被四成的上限縮過）
        }
        onPositionChanged: mouse => {
            if (pressed)
                LibraryFolders.paneWidth = startWidth + Math.round(mapToItem(page, mouse.x, 0).x - startX)
        }
    }

    // ---- 右邊：所選資料夾（含子資料夾）的曲目 ----
    Item {
        id: content
        visible: treeList.visible
        anchors { left: divider.right; right: parent.right; top: parent.top; bottom: parent.bottom }

        // 所選資料夾的路徑（等寬）與曲目數，高度同篩選列
        Item {
            id: pathBar
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.filterBarHeight
            Text {
                objectName: "folderPath"
                anchors { left: parent.left; right: pathCount.left; leftMargin: Theme.space6
                          rightMargin: Theme.space4; verticalCenter: parent.verticalCenter }
                text: LibraryFolders.selectedPath
                font: Theme.monoSmall
                color: Theme.inkMuted
                elide: Text.ElideMiddle
            }
            Text {
                id: pathCount
                anchors { right: parent.right; rightMargin: Theme.space6; verticalCenter: parent.verticalCenter }
                text: page.countText
                font: Theme.monoSmall
                color: Theme.inkMuted
            }
        }

        LibraryTrackList {
            id: trackList
            listName: "folderTrackList"
            visible: LibraryFolders.tracksLoaded && LibraryFolders.tracksError.length === 0
                     && LibraryFolders.trackCount > 0
            anchors { left: parent.left; right: parent.right; top: pathBar.bottom; bottom: parent.bottom }
            model: LibraryFolders.tracks
            allQuery: trackList.selection.folderQuery(LibraryFolders.selectedFolderId)
            onPlayRequested: row => {
                if (row >= 0)
                    LibraryFolders.playFrom(row)
            }
            // 換了資料夾（新內容回來）時回到頂端
            Connections {
                target: LibraryFolders
                function onTracksChanged() {
                    if (LibraryFolders.tracksLoaded)
                        trackList.toTop()
                }
            }
        }

        // 讀取中（超過 150 毫秒才出現，Skeleton 自己處理）
        Column {
            objectName: "folderTracksLoading"
            visible: !LibraryFolders.tracksLoaded
            anchors { left: parent.left; right: parent.right; top: pathBar.bottom }
            anchors.topMargin: Theme.rowHeightCompact
            Repeater {
                model: 6
                delegate: Skeleton {
                    width: parent ? parent.width : 0
                    variant: "row"
                }
            }
        }

        // 資料夾已不存在而且沒有曲目：說明＋重新掃描（音樂庫還有它的曲目時照常列出，樹上標「找不到資料夾」）
        EmptyState {
            objectName: "folderMissing"
            visible: LibraryFolders.tracksLoaded && LibraryFolders.selectedMissing
                     && LibraryFolders.trackCount === 0
            anchors { left: parent.left; right: parent.right; top: pathBar.bottom; bottom: parent.bottom }
            title: qsTr("找不到資料夾")
            description: qsTr("這個資料夾已經不在磁碟上，可能被移動、改名或刪除。")
            actionText: qsTr("重新掃描")
            onAction: Library.rescan()
        }

        EmptyState {
            objectName: "folderNoTracks"
            visible: LibraryFolders.tracksLoaded && LibraryFolders.tracksError.length === 0
                     && !LibraryFolders.selectedMissing && LibraryFolders.trackCount === 0
            anchors { left: parent.left; right: parent.right; top: pathBar.bottom; bottom: parent.bottom }
            title: qsTr("這個資料夾裡沒有曲目")
        }

        ErrorBlock {
            objectName: "folderTracksError"
            visible: LibraryFolders.tracksLoaded && LibraryFolders.tracksError.length > 0
            anchors { left: parent.left; right: parent.right; top: pathBar.bottom; bottom: parent.bottom }
            message: qsTr("無法載入曲目。")
            onRetry: LibraryFolders.reloadTracks()
        }
    }

    // 載入中：左欄是列
    Column {
        objectName: "foldersLoading"
        visible: page.loading
        width: LibraryFolders.paneWidth
        anchors { left: parent.left; top: parent.top }
        Repeater {
            model: 8
            delegate: Skeleton {
                width: parent ? parent.width : 0
                variant: "row"
            }
        }
    }

    // 空：沒有音樂資料夾
    EmptyState {
        objectName: "foldersEmpty"
        visible: LibraryFolders.loaded && treeList.count === 0 && !page.failed
        anchors.fill: parent
        title: qsTr("音樂庫裡還沒有資料夾")
        description: qsTr("加入音樂資料夾後，資料夾會出現在這裡。")
        actionText: qsTr("加入音樂資料夾")
        onAction: Library.chooseFolder()
    }

    ErrorBlock {
        objectName: "foldersError"
        visible: page.failed
        anchors.fill: parent
        message: qsTr("無法載入資料夾。")
        onRetry: LibraryFolders.reload()
    }
}
