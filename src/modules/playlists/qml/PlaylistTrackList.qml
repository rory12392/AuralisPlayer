pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Pl
import "PlaylistListActions.js" as ListActions
import "PlaylistColumns.js" as PlaylistColumns

// 播放清單頁的曲目清單（畫面規格 S-08：同 S-07 的 ListHeader＋TrackRow，多一欄拖曳把手；US-11 AC-1）。
// 資料是單例 PlaylistTracks（頁面設 playlistId）。鍵盤焦點在清單本身（上下鍵、Enter…，列不另外取得焦點）。
// - 選取、右鍵選單（TrackMenu）、拖放來源（TrackDragArea，內容同右鍵選單）照音樂庫清單的寫法。
// - 拖曳排序：按住拖曳把手欄上下拖，放開時一次寫入（PlaylistTracks.moveRows）；被按的列已選取時
//   整個選取一起移。把手是疊在整個清單上的一條（不是每一列一個）：列在捲動時會被回收，
//   放在列裡的話拖到看不見的地方就斷了。靠近上下緣時自動捲動。
// - 接收拖放：從音樂庫拖到清單中間＝插在那個位置，拖到空白處＝加在最後；從本頁拖出來的列不接收
//   （排序用把手）。放下後清單會重讀，所以晚一點才加入（Qt.callLater）。
Item {
    id: view

    readonly property alias list: trackList
    readonly property alias menu: trackMenu
    readonly property var columns: PlaylistColumns.columns()
    readonly property bool showCover: true
    readonly property int rowHeight: showCover ? Theme.rowHeight : Theme.rowHeightCompact
    // 把手欄在列裡的位置（TrackRow：左邊距、播放標記、封面縮圖之後才是第一欄）
    readonly property real handleX: Theme.space6 + TrackColumns.markWidth
                                    + (showCover ? Theme.coverThumb : 0) + Theme.space3
    readonly property int handleWidth: TrackColumns.widths(columns, width - handleX - Theme.space6,
                                                           Theme.space3)[0] ?? 0

    // 拖曳把手排序中：被拖的列與插入位置（移動前的列號；列數＝最後）
    property var reorderRows: []
    property int reorderTarget: -1
    // 從別的地方拖曲目進來時的插入位置
    property int dropTarget: -1
    // 本頁的列正被拖出去（拖回本頁時不接收；排序用把手）
    property bool selfDrag: false
    readonly property int indicatorRow: reorderTarget >= 0 ? reorderTarget : dropTarget

    readonly property var actions: ({
        list: trackList,
        tracks: PlaylistTracks,
        menu: trackMenu,
        player: Player,
        contain: ListView.Contain
    })

    // 清單內容的 y（含捲動）→ 插入位置：落在一列的下半部時插在它後面
    function targetAt(y) {
        if (trackList.count === 0)
            return 0
        const row = trackList.indexAt(Theme.space6, y + trackList.contentY)
        if (row < 0)
            return y + trackList.contentY < 0 ? 0 : trackList.count
        const item = trackList.itemAtIndex(row)
        const lower = item && (y + trackList.contentY - item.y) > item.height / 2
        return lower ? row + 1 : row
    }

    function beginReorder(y) {
        const row = trackList.indexAt(Theme.space6, y + trackList.contentY)
        if (row < 0)
            return
        trackList.currentIndex = row
        trackList.forceActiveFocus()
        PlaylistTracks.ensureSelected(row)
        reorderRows = PlaylistTracks.selectedRows()
        reorderTarget = targetAt(y)
    }

    function finishReorder() {
        const rows = reorderRows
        const target = reorderTarget
        reorderRows = []
        reorderTarget = -1
        if (rows.length === 0 || target < 0)
            return
        const first = PlaylistTracks.moveRows(rows, target)
        if (first >= 0)
            trackList.currentIndex = first
    }

    property var pendingDrop: null
    function dropLater(json, row) {
        pendingDrop = { json: json, row: row }
        Qt.callLater(view.flushDrop)
    }
    function flushDrop() {
        const drop = pendingDrop
        pendingDrop = null
        if (drop)
            PlaylistTracks.insertTracks(drop.json, drop.row >= trackList.count ? -1 : drop.row)
    }

    TrackMenu {
        id: trackMenu
    }

    ListHeader {
        id: listHeader
        anchors { left: parent.left; right: parent.right; top: parent.top }
        columns: view.columns
        showCover: view.showCover
    }

    ListView {
        id: trackList
        objectName: "playlistTrackList"
        anchors { left: parent.left; right: parent.right; top: listHeader.bottom; bottom: parent.bottom }
        clip: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        reuseItems: true
        cacheBuffer: view.rowHeight * 10
        model: PlaylistTracks
        Accessible.role: Accessible.List
        Accessible.name: qsTr("播放清單的曲目")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: trackList; lineHeight: view.rowHeight }

        Keys.onPressed: event => event.accepted = ListActions.handleKey(view.actions, event)

        // 換資料（加入、復原後重讀）時留在原本捲到的位置
        property real keptY: 0
        Connections {
            target: PlaylistTracks
            function onModelAboutToBeReset() { trackList.keptY = trackList.contentY }
            function onModelReset() {
                Qt.callLater(() => trackList.contentY = Math.max(0, Math.min(trackList.keptY,
                                                                        trackList.contentHeight - trackList.height)))
            }
        }

        delegate: TrackDragArea {
            id: drag
            required property int index
            required property var model

            objectName: "playlistTrackDrag"
            width: ListView.view.width
            height: row.height
            count: PlaylistTracks.revision >= 0 ? PlaylistTracks.dragCount(drag.index) : 0
            tracks: drag.dragging ? PlaylistTracks.selectedTracks() : []
            onDraggingChanged: {
                if (drag.dragging) {
                    PlaylistTracks.ensureSelected(drag.index)
                    view.selfDrag = true
                }
            }
            onDragFinished: Qt.callLater(() => view.selfDrag = false)

            TrackRow {
                id: row
                objectName: "playlistTrackRow"
                width: parent.width
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: PlaylistTracks.revision >= 0 && PlaylistTracks.isSelected(drag.index)
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
                missing: drag.model.missing
                // 路徑在磁碟上但不在音樂庫：仍可播放，文字不轉淡（S-08，T-52）
                statusText: drag.model.missing ? qsTr("找不到檔案")
                            : drag.model.notInLibrary ? qsTr("不在音樂庫") : ""
                playing: PlaylistTracks.currentKey.length > 0 && PlaylistTracks.currentKey === drag.model.key
                onClicked: ListActions.click(view.actions, drag.index)
                onDoubleClicked: PlaylistTracks.playFrom(drag.index)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: ListActions.openMenuAt(view.actions, drag.index)
                }
                Icon {
                    x: view.handleX + (view.handleWidth - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    visible: view.handleWidth > 0
                    name: "drag-handle"
                    tone: "muted"
                }
            }
        }
    }

    // 拖曳把手：疊在每一列的把手欄上的一條（見檔頭）
    MouseArea {
        id: handleArea
        objectName: "playlistReorderHandle"
        x: view.handleX
        width: view.handleWidth
        anchors { top: trackList.top; bottom: trackList.bottom }
        visible: view.handleWidth > 0 && trackList.count > 0
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real lastY: 0
        onPressed: mouse => { lastY = mouse.y; view.beginReorder(mouse.y) }
        onPositionChanged: mouse => {
            lastY = mouse.y
            if (view.reorderRows.length > 0)
                view.reorderTarget = view.targetAt(mouse.y)
        }
        onReleased: view.finishReorder()
        onCanceled: { view.reorderRows = []; view.reorderTarget = -1 }
    }

    // 拖到上下緣時自動捲動
    Timer {
        interval: Theme.motionState
        repeat: true
        running: view.reorderRows.length > 0
                 && (handleArea.lastY < view.rowHeight || handleArea.lastY > trackList.height - view.rowHeight)
        onTriggered: {
            const step = handleArea.lastY < view.rowHeight ? -view.rowHeight : view.rowHeight
            trackList.contentY = Math.max(0, Math.min(trackList.contentHeight - trackList.height, trackList.contentY + step))
            view.reorderTarget = view.targetAt(handleArea.lastY)
        }
    }

    // 從音樂庫、佇列以外的地方拖曲目進來（清單與下方空白都算）
    TrackDropArea {
        id: dropZone
        anchors { left: parent.left; right: parent.right; top: trackList.top; bottom: trackList.bottom }
        readonly property bool fromHere: view.selfDrag || drag.source?.objectName === "playlistTrackDrag"
        onPositionChanged: drag => view.dropTarget = fromHere ? -1 : view.targetAt(drag.y)
        onContainsDragChanged: if (!containsDrag) view.dropTarget = -1
        // 放下的位置用放下那一刻的座標算（containsDrag 可能已經先變回 false、清掉 dropTarget）
        onTracksDropped: payload => {
            view.dropTarget = -1
            if (!fromHere)
                view.dropLater(payload.json, view.targetAt(payload.y))
        }
    }

    // 插入位置的線
    Rectangle {
        visible: view.indicatorRow >= 0
        x: Theme.space6
        width: trackList.width - Theme.space6 * 2
        height: Theme.focusWidth
        color: Theme.ink
        y: trackList.y + Math.max(0, Math.min(trackList.height, view.indicatorRow * view.rowHeight - trackList.contentY)) - height / 2
    }
}
