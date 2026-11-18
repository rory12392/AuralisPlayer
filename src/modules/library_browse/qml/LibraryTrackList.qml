pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib
import "ListActions.js" as ListActions
import "TrackColumnCatalog.js" as TrackColumnCatalog

// 曲目清單（S-07 的清單，T-21 從「所有曲目」頁抽出來，T-23 資料夾頁也用）：表頭＋虛擬化的 TrackRow。
// model 的角色同 LibrarySearch.results（title、artist、album、durationText、format、qualityText、
// coverUrl、path、subsong）。鍵盤焦點在清單本身（上下鍵、Enter），列不另外取得焦點（照佇列面板）；
// 雙擊或 Enter 發 playRequested(row)。欄位與欄寬見下方 columns（T-90，「欄位」選單在 TrackColumnsButton）。
// T-25：選取（Shift＋上下鍵、Ctrl＋點擊、Ctrl+A）、Shift+Enter 接著播放、Ctrl+Enter 加到佇列最後、
// 右鍵選單（滑鼠右鍵、選單鍵、Shift+F10，TrackMenu）、拖放來源（TrackDragArea，內容同右鍵選單）。
// 全選超過 5,000 首時拖放與選單只帶 allQuery（頁面給：所有曲目是搜尋條件、資料夾頁是資料夾）。
Item {
    id: view

    property alias model: list.model
    property alias listName: list.objectName
    property alias count: list.count
    property alias currentIndex: list.currentIndex
    property string accessibleName: qsTr("曲目")
    /// 全選時代表整份清單的查詢條件（TrackSelection.allQuery）
    property alias allQuery: selection.allQuery
    /// 清單已全部載入（TrackSelection.complete；分批載入的頁面綁上，沒載完時只有 Ctrl+A 算全選）
    property alias complete: selection.complete
    readonly property alias selection: selection
    readonly property alias menu: menu
    /// 狀態列左側的選取說明（頁面的 statusTextLeft）
    readonly property string selectionText: ListActions.statusText(selection.trackCount)

    signal playRequested(int row)

    // 回到頂端並選第一列（新內容回來時）
    function toTop() {
        list.currentIndex = list.count > 0 ? 0 : -1
        list.positionViewAtBeginning()
    }

    // T-90：欄位與欄寬來自 LibraryColumns（LIB/trackColumns，所有曲目與資料夾頁共用，「欄位」選單改它）；
    // 頁面可以另外指定 columns。寬度不夠時由 TrackRow／ListHeader 依欄的 priority 藏欄（介面規範 §4：
    // 960 寬先藏格式、專輯，資料夾頁右邊約 400 寬時只剩曲名、藝人、長度）。
    readonly property var catalog: TrackColumnCatalog.catalog()
    readonly property var layout: TrackColumns.restore(catalog, LibraryColumns.saved)
    // 拖曳表頭分隔線時的欄位（放開時存進 LibraryColumns 再清掉）
    property var resizing: null
    property var columns: resizing ?? TrackColumnCatalog.columnsOf(layout)
    property bool showCover: TrackColumnCatalog.hasCover(layout)

    TrackSelection {
        id: selection
        model: list.model
    }

    TrackMenu {
        id: menu
    }

    readonly property var actions: ({
        list: list,
        selection: selection,
        menu: menu,
        player: Player,
        contain: ListView.Contain,
        rowStep: 1,
        colStep: 0,
        activate: row => view.playRequested(row)
    })

    // 表頭的分隔線可拖曳改欄寬（只用滑鼠，T-89）；放開時連同封面縮圖的開關一起保存
    ListHeader {
        id: header
        objectName: "libraryTrackHeader"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        columns: view.columns
        showCover: view.showCover
        resizable: true
        onColumnResized: (id, width) => view.resizing = TrackColumns.resized(view.columns, id, width)
        onColumnResizeFinished: (id, width) => {
            const resized = TrackColumns.resized(view.columns, id, width)
            LibraryColumns.saved = TrackColumns.save(view.showCover ? [view.catalog[0]].concat(resized) : resized)
            view.resizing = null
        }
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        clip: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        reuseItems: true
        cacheBuffer: Theme.rowHeight * 10
        Accessible.role: Accessible.List
        Accessible.name: view.accessibleName
        ScrollBar.vertical: ScrollBar {}
        // 列高跟著有沒有封面欄（TrackRow：有封面 rowHeight、沒有 rowHeightCompact）
        WheelScroll { flickable: list; lineHeight: view.showCover ? Theme.rowHeight : Theme.rowHeightCompact }

        Keys.onPressed: event => event.accepted = ListActions.handleKey(view.actions, event)

        // 拖放來源包住列（TrackDragArea 要包住、不能蓋在上面）；拖曳開始時才取曲目
        delegate: TrackDragArea {
            id: drag
            required property int index
            required property var model

            width: ListView.view.width
            height: row.height
            count: selection.revision >= 0 && selection.isSelected(drag.index) ? selection.trackCount : 1
            query: drag.dragging ? selection.query() : ""
            tracks: drag.dragging && !selection.queryOnly ? selection.tracks() : []
            onDraggingChanged: {
                if (drag.dragging)
                    selection.ensureSelected(drag.index)
            }

            TrackRow {
                id: row
                width: parent.width
                // 鍵盤焦點在清單本身（上下鍵、Enter 由清單處理），列不另外取得焦點
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: selection.revision >= 0 && selection.isSelected(drag.index)
                columns: view.columns
                showCover: view.showCover
                record: drag.model
                title: drag.model.title
                artist: drag.model.artist
                album: drag.model.album
                duration: drag.model.durationText
                format: drag.model.format
                sampleRate: drag.model.qualityText
                coverSource: drag.model.coverUrl
                playing: LibraryPlayback.currentKey.length > 0
                         && LibraryPlayback.currentKey === LibraryPlayback.keyOf(drag.model.path, drag.model.subsong)
                onClicked: ListActions.click(view.actions, drag.index)
                onDoubleClicked: view.playRequested(drag.index)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: ListActions.openMenuAt(view.actions, drag.index)
                }
            }
        }
    }
}
