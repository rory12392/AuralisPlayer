import QtQuick
import QtTest
import Auralis.Ui

// 清單的欄位清單（T-89，AC-10-5「欄位可增減」）：TrackRow 的自訂欄、ListHeader 拖曳欄寬、最小寬、
// 窄視窗藏欄、TrackColumns 的保存與還原。像頁面一樣自己保管欄位清單：拖曳時換掉清單、拖完保存。
Item {
    id: root
    width: 900
    height: 400

    // 既有用法：只用固定槽位、不給 record
    TrackRow {
        id: row
        width: 800
        columns: [{ id: "title", title: "曲名", weight: 2 }, { id: "artist", title: "藝人", weight: 1 },
                  { id: "duration", title: "長度", width: 64, align: "right", sortable: false }]
        title: "夜に駆ける"
        artist: "YOASOBI"
        duration: "4:21"
    }

    Item {
        id: columnsPage
        y: 80
        width: 800
        height: 100

        readonly property var catalog: [
            { id: "title", title: "曲名", weight: 3 },
            { id: "year", title: "年份", width: 64, align: "right", mono: true },
            { id: "composer", title: "作曲者", role: "composerName", weight: 2 },
            { id: "bitrate", title: "位元率", width: 96, value: record => record ? record.kbps + " kbps" : "" },
            { id: "path", title: "路徑", weight: 2, optional: true }
        ]
        property var columns: catalog.filter(column => column.optional !== true)
        property string saved

        ListHeader {
            id: resizableHeader
            width: parent.width
            resizable: true
            columns: columnsPage.columns
            onColumnResized: (id, width) => columnsPage.columns = TrackColumns.resized(columnsPage.columns, id, width)
            onColumnResizeFinished: columnsPage.saved = TrackColumns.save(columnsPage.columns)
        }

        TrackRow {
            id: customRow
            y: 40
            width: parent.width
            columns: columnsPage.columns
            title: "夜に駆ける"
            record: ({ year: 2019, composerName: "Ayase", kbps: 1411 })
        }
    }

    SignalSpy { id: columnResized; target: resizableHeader; signalName: "columnResized" }
    SignalSpy { id: columnResizeFinished; target: resizableHeader; signalName: "columnResizeFinished" }

    // 用滑鼠拖表頭的分隔線 dx 像素
    function dragDivider(test, id, dx) {
        const divider = test.findChild(resizableHeader, "columnDivider_" + id)
        test.verify(divider !== null && divider.visible, "找不到分隔線 " + id)
        const from = divider.mapToItem(root, divider.width / 2, divider.height / 2)
        test.mousePress(root, from.x, from.y)
        for (let i = 1; i <= 8; ++i)
            test.mouseMove(root, from.x + dx * i / 8, from.y)
        test.mouseRelease(root, from.x + dx, from.y)
    }

    function widthOf(id) {
        return resizableHeader.columnWidths[columnsPage.columns.findIndex(column => column.id === id)]
    }

    TestCase {
        name: "TrackColumns"
        when: windowShown

        function init() {
            columnsPage.columns = columnsPage.catalog.filter(column => column.optional !== true)
            columnsPage.saved = ""
            customRow.width = columnsPage.width
            columnResized.clear()
            columnResizeFinished.clear()
        }

        function cell(row, id) {
            return findChild(row, "trackCell_" + id)
        }

        // AC-10-5：自訂欄的值由呼叫端給（role 或 value 函式），對齊、等寬字照欄位清單
        function test_custom_columns_show_record_values() {
            compare(cell(customRow, "title").text, "夜に駆ける")
            compare(cell(customRow, "year").text, "2019")
            compare(cell(customRow, "year").horizontalAlignment, Text.AlignRight)
            compare(cell(customRow, "year").font.family, Theme.monoSmall.family)
            compare(cell(customRow, "composer").text, "Ayase")
            compare(cell(customRow, "composer").font.family, Theme.body.family)
            compare(cell(customRow, "bitrate").text, "1411 kbps")
            customRow.record = { year: 2020, composerName: "", kbps: 320 }
            compare(cell(customRow, "year").text, "2020")
            compare(cell(customRow, "composer").text, "")
            compare(cell(customRow, "bitrate").text, "320 kbps")
            customRow.record = { year: 2019, composerName: "Ayase", kbps: 1411 }
            compare(customRow.columnWidths, resizableHeader.columnWidths)
            compare(findChild(resizableHeader, "headerCell_composer").text, "作曲者")
        }

        // 既有用法不用改：固定槽位照舊從同名屬性取值，長度欄等寬靠右
        function test_fixed_slots_unchanged() {
            compare(cell(row, "title").text, "夜に駆ける")
            compare(cell(row, "artist").text, "YOASOBI")
            compare(cell(row, "duration").text, "4:21")
            compare(cell(row, "duration").font.family, Theme.monoSmall.family)
            compare(cell(row, "duration").horizontalAlignment, Text.AlignRight)
            // 一般寬度時的欄寬和以前的算法一樣：固定寬照給，剩下的依 weight 分
            const widths = row.columnWidths
            const rest = row.width - row.leftPadding - row.rightPadding - 16 - Theme.coverThumb - Theme.space3
                         - 64 - Theme.space3 * 2
            compare(widths[0], Math.floor(rest * 2 / 3))
            compare(widths[1], Math.floor(rest / 3))
            compare(widths[2], 64)
        }

        // 拖曳分隔線改寬度並發出訊號；頁面換掉清單後表頭與列一起變；放開時發出一次 Finished
        function test_drag_divider_resizes_and_signals() {
            const yearBefore = root.widthOf("year")
            const titleBefore = root.widthOf("title")
            root.dragDivider(this, "year", 40)
            verify(columnResized.count > 0)
            compare(columnResizeFinished.count, 1)
            compare(columnResizeFinished.signalArguments[0][0], "year")
            compare(columnResizeFinished.signalArguments[0][1], yearBefore + 40)
            compare(root.widthOf("year"), yearBefore + 40)
            // 曲名欄吃剩下的寬度，所以變窄
            verify(root.widthOf("title") < titleBefore)
            compare(customRow.columnWidths, resizableHeader.columnWidths)
            compare(JSON.parse(columnsPage.saved)[1].width, yearBefore + 40)
            // 曲名欄右邊的分隔線改的是下一欄：往右拖，年份欄變窄、曲名變寬
            columnResizeFinished.clear()
            const titleMid = root.widthOf("title")
            root.dragDivider(this, "title", 30)
            compare(columnResizeFinished.signalArguments[0][0], "year")
            compare(root.widthOf("year"), yearBefore + 10)
            verify(root.widthOf("title") > titleMid)
            // 分隔線不進 Tab 順序（欄寬只用滑鼠調）
            const divider = findChild(resizableHeader, "columnDivider_year")
            verify(!divider.activeFocusOnTab)
        }

        // 最小寬：往左拖到底停在 48；往右拖到底時其他 weight 欄仍保有最小寬、不藏欄
        function test_minimum_width() {
            root.dragDivider(this, "year", -400)
            compare(root.widthOf("year"), 48)
            root.dragDivider(this, "year", 1200)
            for (let i = 0; i < columnsPage.columns.length; ++i)
                verify(resizableHeader.columnWidths[i] >= 48, columnsPage.columns[i].id + " 只剩 "
                                                               + resizableHeader.columnWidths[i])
            compare(customRow.columnWidths, resizableHeader.columnWidths)
        }

        // 窄視窗：放不下時依 priority 藏欄（曲名不藏），最右欄的 statusText 跟著換到顯示中的最後一欄
        function test_narrow_hides_low_priority_columns() {
            customRow.width = 260
            const widths = customRow.columnWidths
            verify(widths[0] >= 48)
            let total = 0
            let shown = 0
            for (const width of widths) {
                total += width
                if (width > 0)
                    ++shown
            }
            verify(shown < widths.length)
            verify(total + Theme.space3 * (shown - 1) <= 260 - 48 - 16 - Theme.coverThumb - Theme.space3)
            verify(!cell(customRow, "composer").visible || !cell(customRow, "bitrate").visible)
            customRow.missing = true
            verify(widths[customRow.lastShown] > 0)
            const last = customRow.columns[customRow.lastShown].id
            compare(cell(customRow, last).text, "找不到檔案")
            customRow.missing = false
        }

        // 保存與還原：照存檔的順序與寬度；壞掉的存檔回到目錄的預設欄；曲名欄一定在
        function test_save_and_restore() {
            const catalog = columnsPage.catalog
            const restored = TrackColumns.restore(catalog, '[{"id":"path"},{"id":"year","width":90},'
                                                  + '{"id":"nope"},{"id":"year","width":10}]')
            compare(restored.map(column => column.id).join(","), "title,path,year")
            compare(restored[2].width, 90)
            compare(TrackColumns.restore(catalog, "壞掉").map(column => column.id).join(","),
                    "title,year,composer,bitrate")
            compare(TrackColumns.restore(catalog, '[{"id":"year","width":5}]')[1].width, 48)
            const json = TrackColumns.save(TrackColumns.resized(columnsPage.columns, "composer", 150))
            compare(json, '[{"id":"title"},{"id":"year","width":64},{"id":"composer","width":150},'
                    + '{"id":"bitrate","width":96}]')
            compare(TrackColumns.restore(catalog, json)[2].width, 150)
        }
    }
}
