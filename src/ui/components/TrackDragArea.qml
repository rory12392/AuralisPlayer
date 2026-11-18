import QtQuick

// 跨模組拖放曲目的來源（介面規範 §3）：把清單列包在裡面（TrackDragArea { TrackRow {…} }），
// 拖曳超過系統的拖曳門檻才開始，列照常收到點擊與雙擊。不要當成另一層蓋在列上面：那樣列收不到點擊
// （T-14 實測）。拖曳中跟著游標的是「N 首曲目」的小卡。
// 內容是 TrackDragPayload 的 JSON（MIME application/x-auralis-tracks）：tracks 是 [{path, subsong}]，
// count 是拖曳的曲目數；全選超過 TrackDragCodec.maxTracks 首時頁面只要給 query（LibraryQuery 的查詢條件），
// 不用列出曲目，接收端自己去查，所以 4 萬首也不會卡住介面。
// 鍵盤沒有拖放：同樣的事用右鍵選單的「加入播放清單」做（介面規範 §7）。
Item {
    id: area

    property var tracks: []
    property string query
    property int count: tracks.length
    // 拖曳開始時才編碼，平常不佔時間
    // 只帶查詢條件時連 tracks 都不轉給 C++（4 萬筆的陣列轉換本身就會卡）
    readonly property bool queryOnly: count > TrackDragCodec.maxTracks && query.length > 0
    readonly property string payloadJson: chip.Drag.active
                                          ? TrackDragCodec.encode(queryOnly ? [] : tracks, query, count) : ""
    readonly property bool dragging: handler.active
    signal dragFinished(int action)

    DragHandler {
        id: handler
        enabled: area.enabled && area.count > 0
        target: null
        // 不搶底下的列的點擊：只有真的拖動才取得獨占
        grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.CanTakeOverFromHandlersOfDifferentType

        onActiveChanged: {
            if (active) {
                chip.parent = area.Window.contentItem ?? area
                area.moveChip()
                chip.Drag.start()
            } else {
                area.dragFinished(chip.Drag.drop())
                chip.parent = area
            }
        }
        onCentroidChanged: if (active) area.moveChip()
    }

    function moveChip() {
        const point = handler.centroid.scenePosition
        const local = chip.parent.mapFromItem(null, point.x, point.y)
        chip.x = local.x + Theme.space3
        chip.y = local.y + Theme.space3
    }

    // 拖曳中跟著游標的小卡；Drag 的熱點在小卡左上角之外一點，也就是游標的位置
    Rectangle {
        id: chip
        visible: area.dragging
        z: 1000
        width: chipText.implicitWidth + 2 * Theme.space3
        height: Theme.chipHeight
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink
        radius: Theme.radius0

        Drag.dragType: Drag.Internal
        Drag.hotSpot.x: -Theme.space3
        Drag.hotSpot.y: -Theme.space3
        Drag.keys: [TrackDragCodec.mimeType]
        Drag.source: area
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.proposedAction: Qt.CopyAction
        Drag.mimeData: ({ [TrackDragCodec.mimeType]: area.payloadJson })

        Text {
            id: chipText
            anchors.centerIn: parent
            text: qsTr("%1 首曲目").arg(Number(area.count).toLocaleString(Qt.locale(), "f", 0))
            font: Theme.small
            color: Theme.ink
        }
    }
}
