import QtQuick
import QtTest
import Auralis.Ui

// 清單與拖放類共用元件（介面規範 §3、§7、§10 第 7 項）：TrackRow、ListHeader、SidebarItem、
// TrackDragArea、TrackDropArea。拖放用滑鼠事件實際拖一次：來源與目標不互相認識，只靠 TrackDragPayload。
Item {
    id: root
    width: 900
    height: 600

    function focusItem() {
        return root.Window.activeFocusItem
    }

    property var lastPayload: null
    property var probe: null

    Button { id: start; text: "起點" }

    ListHeader {
        id: header
        y: 40
        width: 800
        sortColumn: "title"
        columns: [{ id: "title", title: "曲名", weight: 2 }, { id: "artist", title: "藝人", weight: 1 },
                  { id: "duration", title: "長度", width: 64, align: "right", sortable: false }]
    }

    TrackRow {
        id: row
        y: 80
        width: 800
        columns: header.columns
        title: "夜に駆ける"
        artist: "YOASOBI"
        duration: "4:21"
        playing: true
    }

    // 拖放來源：包住一列（列照常收到點擊；蓋在列上面的寫法會擋住點擊，T-14 實測）
    TrackDragArea {
        id: dragArea
        y: 140
        width: 300
        height: 44
        tracks: [{ path: "C:\\音楽\\01 Aria.flac", subsong: 0 }, { path: "D:\\cue\\album.cue", subsong: 3 }]
        TrackRow {
            id: sourceTrack
            anchors.fill: parent
            columns: [{ id: "title", title: "曲名" }]
            title: "Aria"
        }
    }

    // 全選 4 萬首：只帶查詢條件
    Rectangle {
        id: bulkSource
        y: 200
        width: 300
        height: 44
        TrackDragArea {
            id: bulkArea
            anchors.fill: parent
            count: 40000
            query: "all"
        }
    }

    SidebarItem {
        id: playlistItem
        x: 500
        y: 140
        width: 232
        text: "通勤"
        dropTarget: dropArea.containsDrag
        TrackDropArea {
            id: dropArea
            anchors.fill: parent
            onTracksDropped: payload => root.lastPayload = payload
        }
    }

    // §10 第 7 項的探針：一般的 DropArea 看得到什麼（formats 與 mimeData）
    DropArea {
        id: rawDrop
        x: 500
        y: 260
        width: 232
        height: 44
        keys: [TrackDragCodec.mimeType]
        onDropped: drop => root.probe = { formats: drop.formats, data: drop.getDataAsString(TrackDragCodec.mimeType),
                                          sourceJson: (drop.source as TrackDragArea)?.payloadJson ?? "" }
    }

    SignalSpy { id: sortRequested; target: header; signalName: "sortRequested" }
    SignalSpy { id: rowClicked; target: sourceTrack; signalName: "clicked" }

    // 用滑鼠從 source 拖到 target 的中間（test 是發出事件的 TestCase）
    function dragTo(test, source, target) {
        const from = source.mapToItem(root, 20, source.height / 2)
        const to = target.mapToItem(root, target.width / 2, target.height / 2)
        test.mousePress(root, from.x, from.y)
        const steps = 10
        for (let i = 1; i <= steps; ++i)
            test.mouseMove(root, from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps)
        test.mouseRelease(root, to.x, to.y)
    }

    TestCase {
        name: "ListHeader"
        when: windowShown

        function test_sort_by_keyboard_and_unsortable_column() {
            sortRequested.clear()
            start.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "曲名")
            compare(root.focusItem().Accessible.name, "曲名，遞增排序")
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "藝人")
            keyClick(Qt.Key_Space)
            compare(sortRequested.count, 1)
            compare(sortRequested.signalArguments[0][0], "artist")
            // 不能排序的欄不在 Tab 順序裡
            keyClick(Qt.Key_Tab)
            verify(root.focusItem().text !== "長度")
        }
    }

    TestCase {
        name: "TrackRow"
        when: windowShown

        function test_columns_align_with_header_and_states() {
            // 表頭與列用同一份欄寬：每欄左緣一致
            compare(row.columnWidths, header.columnWidths)
            compare(row.Accessible.name, "夜に駆ける YOASOBI 4:21 正在播放")
            row.missing = true
            compare(row.statusText, "找不到檔案")
            verify(row.Accessible.name.indexOf("找不到檔案") >= 0)
            row.missing = false
            // T-29：封面的無障礙名稱預設是專輯名；借用槽位的清單（專輯清單把年份放 album）給 coverName
            const thumb = findChild(row, "trackRowCover")
            verify(thumb !== null)
            row.album = "THE BOOK"
            compare(thumb.Accessible.name, "THE BOOK")
            row.album = "2021"
            row.coverName = "THE BOOK"
            compare(thumb.Accessible.name, "THE BOOK")
            row.coverName = ""
            row.album = ""
            start.forceActiveFocus(Qt.TabFocusReason)
            row.forceActiveFocus(Qt.TabFocusReason)
            tryVerify(() => row.activeFocus)
            verify(row.visualFocus)
        }
    }

    TestCase {
        name: "SidebarItem"
        when: windowShown

        function test_drop_target_state() {
            verify(!playlistItem.dropTarget)
            compare(playlistItem.Accessible.name, "通勤")
            playlistItem.count = "48"
            compare(playlistItem.Accessible.name, "通勤 48")
        }
    }

    TestCase {
        name: "TrackDragArea"
        when: windowShown

        function test_click_still_reaches_row() {
            rowClicked.clear()
            mouseClick(dragArea, 20, 20)
            compare(rowClicked.count, 1)
            verify(!dragArea.dragging)
        }

        function test_bulk_drag_carries_query_only() {
            root.lastPayload = null
            const timer = Date.now()
            root.dragTo(this, bulkSource, playlistItem)
            tryVerify(() => root.lastPayload !== null)
            // 4 萬首拖曳從按下到放下不卡（含模擬滑鼠移動）
            verify(Date.now() - timer < 1000, "拖曳花了 " + (Date.now() - timer) + " ms")
            compare(root.lastPayload.count, 40000)
            compare(root.lastPayload.tracks.length, 0)
            compare(root.lastPayload.query, "all")
            verify(root.lastPayload.json.indexOf("\"query\":\"all\"") >= 0)
        }

        function test_over_max_without_query_lists_tracks() {
            const many = []
            for (let i = 0; i < 6000; ++i)
                many.push({ path: "C:\\m\\" + i + ".flac", subsong: 0 })
            const json = TrackDragCodec.encode(many, "", many.length)
            compare(TrackDragCodec.decode(json).tracks.length, 6000)
            const withQuery = TrackDragCodec.encode(many, "all", many.length)
            compare(TrackDragCodec.decode(withQuery).tracks.length, 0)
        }
    }

    TestCase {
        name: "TrackDropArea"
        when: windowShown

        function test_drop_delivers_payload_and_highlights_target() {
            root.lastPayload = null
            const from = dragArea.mapToItem(root, 20, 22)
            const to = playlistItem.mapToItem(root, 100, 18)
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 10; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10)
            // 拖到上面時側欄項目是拖曳目標
            tryVerify(() => playlistItem.dropTarget)
            verify(dragArea.dragging)
            mouseRelease(root, to.x, to.y)
            tryVerify(() => root.lastPayload !== null)
            verify(!playlistItem.dropTarget)
            compare(root.lastPayload.count, 2)
            compare(root.lastPayload.tracks[0].path, "C:\\音楽\\01 Aria.flac")
            compare(root.lastPayload.tracks[1].subsong, 3)
            // JSON 是 TrackDragPayload 的格式，C++ 端可以直接 fromJson
            compare(TrackDragCodec.decode(root.lastPayload.json).valid, true)
        }

        function test_rejects_foreign_content() {
            compare(TrackDragCodec.decode("{\"x\":1}").valid, false)
            compare(TrackDragCodec.decode("not json").valid, false)
        }

        // 介面規範 §10 第 7 項：視窗內拖曳（Drag.Internal）時 DropArea 看得到 keys，但讀不到 Drag.mimeData，
        // 所以 TrackDropArea 從 drop.source 取內容
        function test_internal_drag_exposes_keys_not_mime_data() {
            root.probe = null
            root.dragTo(this, dragArea, rawDrop)
            tryVerify(() => root.probe !== null)
            verify(root.probe.formats.indexOf(TrackDragCodec.mimeType) >= 0)
            compare(root.probe.data, "")
            verify(root.probe.sourceJson.length > 0)
        }
    }
}
