import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 曲目清單的「欄位」選單與可調欄寬（T-90，US-10 AC-5；畫面規格 S-07）：選單列出預設欄與可加的欄、
// 勾選即增減、曲名不能拿掉；鍵盤 Tab 到「欄位」、上下鍵與空白鍵／Enter 勾選；表頭拖曳欄寬放開時保存；
// 所有曲目與資料夾頁共用 LibraryColumns；寬度不夠時依 priority 藏欄（取代以前的 < 760／< 520 過濾）。
// T-115：可加「位元率」「加入時間」，補值中空白；所有曲目頁按這兩欄的表頭排序（遞增 → 遞減 → 預設）。
Item {
    id: root
    width: 1100
    height: 760

    Button { id: start; text: "起點" }

    TracksPage {
        id: tracksPage
        y: 40
        width: 900
        height: 300
    }

    // 另一份清單（資料夾頁用的同一個元件）：確認兩頁共用設定
    LibraryTrackList {
        id: other
        y: 360
        width: 900
        height: 200
        model: LibrarySearch.results
    }

    TestCase {
        name: "TrackColumns"
        when: windowShown

        property var list: null
        property var toolbar: null

        function ids(columns) {
            return columns.map(column => column.id).join(",")
        }

        function savedIds() {
            return JSON.parse(LibraryColumns.saved).map(entry => entry.id).join(",")
        }

        function initTestCase() {
            list = findChild(tracksPage, "libraryTrackList").parent
            const row = tracksPage.toolbar.createObject(root, { x: 120, height: 40 })
            toolbar = findChild(row, "trackColumnsButton")
            verify(toolbar)
        }

        function init() {
            LibraryColumns.saved = ""
            tryVerify(() => !LibrarySearch.busy && LibrarySearch.count > 0, 5000)
            start.forceActiveFocus()
        }

        function cleanup() {
            const menu = findChild(toolbar, "trackColumnsMenu")
            if (menu.opened)
                menu.close()
            LibraryColumns.saved = ""
            LibrarySearch.sortBy("", false)
            tracksPage.height = 300
        }

        function openMenu() {
            const menu = findChild(toolbar, "trackColumnsMenu")
            mouseClick(toolbar)
            tryVerify(() => menu.opened)
            return menu
        }

        function test_default_columns() {
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate")
            verify(list.showCover)
        }

        function test_menu_lists_catalog() {
            const menu = openMenu()
            const titles = []
            for (let i = 0; i < menu.count; ++i)
                titles.push(menu.itemAt(i).text)
            compare(titles.join(","),
                    "封面縮圖,曲名,藝人,專輯,長度,格式,取樣率／位元,專輯藝人,作曲者,年份,曲序,位元率,加入時間,路徑")
            const title = findChild(menu, "trackColumn_title")
            verify(title.checked)
            verify(!title.enabled) // 曲名不能拿掉
            verify(findChild(menu, "trackColumn_artist").checked)
            verify(!findChild(menu, "trackColumn_year").checked)
        }

        function test_toggle_adds_and_removes() {
            let menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_path"))
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate,path")
            compare(ids(other.columns), ids(list.columns)) // 資料夾頁跟著變
            const cell = findChild(list, "trackCell_path")
            verify(cell.text.startsWith("C:/t/"))

            menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_album"))
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,artist,duration,format,sampleRate,path")

            // 加回來時照目錄的順序插回原位
            menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_album"))
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate,path")

            // 封面縮圖是 showCover，不是文字欄
            menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_cover"))
            tryVerify(() => !menu.opened)
            verify(!list.showCover)
            verify(!other.showCover)
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate,path")
            verify(savedIds().indexOf("cover") < 0)
        }

        function test_year_and_track_number_values() {
            LibraryColumns.saved = JSON.stringify([{ id: "cover" }, { id: "title" }, { id: "trackNo" }, { id: "year" }])
            compare(ids(list.columns), "title,trackNo,year")
            const trackNo = findChild(list, "trackCell_trackNo")
            verify(/^[0-9]+$/.test(trackNo.text))
            verify(trackNo.font.family === Theme.monoSmall.family)
        }

        // T-115：第 row 列某一欄的文字（清單要先撐高，讓 16 列都建立）
        function cellText(row, id) {
            const view = findChild(tracksPage, "libraryTrackList")
            // 頁面剛撐高時，下面的列要等下一次排版才建立
            tryVerify(() => view.itemAtIndex(row) !== null, 2000, "第 " + row + " 列沒有建立")
            return findChild(view.itemAtIndex(row), "trackCell_" + id).text
        }

        function bitrateColumns() {
            LibraryColumns.saved = JSON.stringify([{ id: "cover" }, { id: "title" }, { id: "bitrate" }, { id: "addedAt" }])
        }

        function test_bitrate_and_added_columns() {
            // 勾選兩欄（選單），存進 LibraryColumns；重新啟動時從存檔字串還原（目錄認得這兩欄）
            let menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_bitrate"))
            tryVerify(() => !menu.opened)
            menu = openMenu()
            mouseClick(findChild(menu, "trackColumn_addedAt"))
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate,bitrate,addedAt")
            verify(savedIds().endsWith("bitrate,addedAt"))
            compare(ids(other.columns), ids(list.columns))

            bitrateColumns()
            compare(ids(list.columns), "title,bitrate,addedAt")
            tracksPage.height = 720
            // 第 0 列：專輯 1 第 1 首；第 2 列：專輯 2（位元率補值中）；第 8 列：專輯 5（加入時間補值中）
            compare(cellText(0, "bitrate"), "2,899 kbps")
            compare(cellText(0, "addedAt"), "2026-01-01")
            compare(cellText(2, "bitrate"), "")
            compare(cellText(2, "addedAt"), "2026-01-02")
            compare(cellText(8, "addedAt"), "")
            const cell = findChild(list, "trackCell_bitrate")
            verify(cell.font.family === Theme.monoSmall.family)
            tracksPage.height = 300
        }

        function test_sort_by_header() {
            bitrateColumns()
            tracksPage.height = 720
            const header = findChild(list, "libraryTrackHeader")
            const bitrate = findChild(header, "headerCell_bitrate")
            verify(bitrate.enabled)
            verify(!findChild(header, "headerCell_title").enabled) // 其他欄照舊不能排序

            // 第一次按：遞增；沒有位元率的（專輯 2）排最後
            // 表頭的 Row 換了欄位要等下一次排版才擺好位置（之前按下去會按到別欄）
            tryVerify(() => bitrate.x > 0)
            mouseClick(bitrate)
            tryVerify(() => LibrarySearch.sort === "bitrate" && !LibrarySearch.busy && LibrarySearch.complete)
            compare(header.sortColumn, "bitrate")
            verify(header.sortAscending)
            compare(bitrate.Accessible.name, "位元率，遞增排序")
            tryVerify(() => cellText(0, "bitrate") === "2,198 kbps")
            compare(cellText(13, "bitrate"), "2,899 kbps")
            compare(cellText(14, "bitrate"), "")
            compare(cellText(15, "bitrate"), "")

            // 第二次：遞減，空白照樣在最後
            mouseClick(bitrate)
            tryVerify(() => LibrarySearch.descending && !LibrarySearch.busy && LibrarySearch.complete)
            verify(!header.sortAscending)
            tryVerify(() => cellText(0, "bitrate") === "2,899 kbps")
            compare(cellText(13, "bitrate"), "2,198 kbps")
            compare(cellText(15, "bitrate"), "")

            // 第三次：回到預設順序
            mouseClick(bitrate)
            tryVerify(() => LibrarySearch.sort === "" && !LibrarySearch.busy && LibrarySearch.complete)
            compare(header.sortColumn, "")
            tryVerify(() => cellText(2, "bitrate") === "")
            compare(cellText(0, "bitrate"), "2,899 kbps")

            // 加入時間：遞增，補值中的（專輯 5）在最後
            mouseClick(findChild(header, "headerCell_addedAt"))
            tryVerify(() => LibrarySearch.sort === "addedAt" && !LibrarySearch.busy && LibrarySearch.complete)
            tryVerify(() => cellText(0, "addedAt") === "2026-01-01")
            compare(cellText(13, "addedAt"), "2026-01-08")
            compare(cellText(14, "addedAt"), "")

            // 拿掉正在排序的欄（加入時間）：回到預設順序
            LibraryColumns.saved = JSON.stringify([{ id: "cover" }, { id: "title" }, { id: "bitrate" }])
            tryVerify(() => LibrarySearch.sort === "")
            tracksPage.height = 300
        }

        function test_sort_by_keyboard() {
            // 可排序的表頭進 Tab 順序，空白鍵換排序（共用 ListHeader 的欄是按鈕，Enter 不觸發）
            bitrateColumns()
            const cell = findChild(findChild(list, "libraryTrackHeader"), "headerCell_addedAt")
            verify(cell.activeFocusOnTab)
            tryVerify(() => cell.x > 0)
            cell.forceActiveFocus()
            verify(cell.activeFocus)
            keyClick(Qt.Key_Space)
            tryVerify(() => LibrarySearch.sort === "addedAt" && !LibrarySearch.descending)
            keyClick(Qt.Key_Space)
            tryVerify(() => LibrarySearch.descending)
        }

        function test_sort_only_on_tracks_page() {
            // 資料夾頁（同一個元件、沒有接排序）的兩欄表頭不能按
            bitrateColumns()
            verify(!findChild(other, "headerCell_bitrate").enabled)
            verify(!findChild(other, "headerCell_addedAt").enabled)
        }

        function test_keyboard() {
            // Tab 到「欄位」、空白鍵開選單、上下鍵移動、空白鍵與 Enter 勾選
            start.forceActiveFocus()
            toolbar.forceActiveFocus()
            verify(toolbar.activeFocus)
            const menu = findChild(toolbar, "trackColumnsMenu")
            keyClick(Qt.Key_Space)
            tryVerify(() => menu.opened)
            // 第一項是封面縮圖；往下到「藝人」（曲名停用會被跳過）
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 2)
            keyClick(Qt.Key_Space)
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,album,duration,format,sampleRate")
            tryVerify(() => toolbar.activeFocus) // 關閉後焦點回按鈕

            keyClick(Qt.Key_Return)
            tryVerify(() => menu.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(() => !menu.opened)
            compare(ids(list.columns), "title,artist,album,duration,format,sampleRate")
        }

        function test_resize_saves_on_release() {
            const header = findChild(list, "libraryTrackHeader")
            header.columnResized("artist", 200)
            compare(list.columns[1].width, 200)
            compare(other.columns[1].width, undefined) // 拖曳中還沒保存
            header.columnResizeFinished("artist", 210)
            compare(JSON.parse(LibraryColumns.saved)[2].width, 210)
            compare(other.columns[1].width, 210)
            verify(list.showCover)
            compare(list.resizing, null)
        }

        function test_narrow_hides_by_priority() {
            // 960 寬視窗的所有曲目頁約 700 寬：先藏格式、再藏專輯（介面規範 §4 的順序）
            function shown(view) {
                const header = findChild(view, "libraryTrackHeader")
                return view.columns.filter((column, i) => header.columnWidths[i] > 0).map(column => column.id).join(",")
            }
            // 頁面裡的清單寬度跟著頁面（anchors），所以用另一份清單改寬度
            other.width = 900
            compare(shown(other), "title,artist,album,duration,format,sampleRate")
            other.width = 700
            compare(shown(other), "title,artist,album,duration,sampleRate")
            other.width = 640
            compare(shown(other), "title,artist,duration,sampleRate")
            // 資料夾頁 960 寬時右邊約 400：只剩曲名、藝人、長度（同以前的 < 520）
            other.width = 400
            compare(shown(other), "title,artist,duration")
            other.width = 900
        }
    }
}
