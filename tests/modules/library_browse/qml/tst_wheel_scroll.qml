import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 音樂庫清單的滾輪捲動（fix-m02-wheel，共用元件 WheelScroll，介面規範 §3）：一格滾輪捲
// 「Windows 一次捲動的行數」× 列高（封面牆是格高）；各頁所有可捲動的清單都放了 WheelScroll。
// 專輯詳細頁要登記頁面，放在 tst_detail_page.qml。
Item {
    id: root
    width: 1100
    height: 760

    function find(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            const found = find(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    // item 底下所有的 Flickable（ListView、GridView 也是）
    function flickables(item, out) {
        if (!item)
            return out
        if ("flickableDirection" in item && "contentY" in item)
            out.push(item)
        const kids = item.children
        for (let i = 0; i < kids.length; ++i)
            flickables(kids[i], out)
        return out
    }

    // view 直接底下的 WheelScroll（有 flickable 屬性指回 view、有 scroll 函式）
    function wheelOf(view) {
        const kids = view.children
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].flickable === view && typeof kids[i].scroll === "function")
                return kids[i]
        }
        return null
    }

    // 窄頁面：封面牆一欄、格子多列，捲得動 3 格
    AlbumsPage {
        id: albumsPage
        width: 240
        height: 420
    }

    TracksPage {
        id: tracksPage
        x: 260
        width: 800
        height: 240
    }

    ArtistsPage {
        id: artistsPage
        x: 260
        y: 260
        width: 800
        height: 220
    }

    FoldersPage {
        id: foldersPage
        x: 260
        y: 500
        width: 800
        height: 220
    }

    TestCase {
        name: "LibWheelScroll"
        when: windowShown

        readonly property int lines: Application.styleHints.wheelScrollLines

        function init() {
            LibrarySearch.clearFilters()
            LibraryAlbums.view = "grid"
            LibraryAlbums.sort = "added"
            tryVerify(() => LibraryAlbums.loaded && LibraryAlbums.count === 8, 5000)
            tryVerify(() => LibraryArtists.loaded && LibraryFolders.loaded, 5000)
            // 新查詢的結果回來時所有曲目會回到頂端：等查詢結束再捲
            tryVerify(() => !LibrarySearch.busy && LibrarySearch.complete && LibrarySearch.count === 16, 5000)
        }

        // 一格滾輪（angleDelta 120）送到清單中間，回傳 contentY 增加多少（等一下，確認沒有再滑行或被拉回）
        function notch(view) {
            view.contentY = view.originY - view.topMargin
            const before = view.contentY
            mouseWheel(view, view.width / 2, view.height / 2, 0, -120)
            wait(100)
            return view.contentY - before
        }

        function test_album_grid_scrolls_rows_of_cells() {
            verify(lines > 0, "Windows 一次捲動的行數：" + lines)
            const grid = root.find(albumsPage, "albumGrid")
            verify(grid, "封面牆")
            tryVerify(() => grid.count === 8)
            compare(grid.columns, 1)
            const max = grid.contentHeight + grid.topMargin + grid.bottomMargin - grid.height
            verify(max >= lines * grid.cellHeight, "內容夠捲 " + lines + " 格：" + max)
            fuzzyCompare(notch(grid), lines * grid.cellHeight, 0.5)
        }

        function test_all_tracks_scrolls_rows() {
            const view = root.find(tracksPage, "libraryTrackList")
            verify(view, "所有曲目的清單")
            tryVerify(() => view.count === 16, 5000)
            const rowHeight = view.itemAtIndex(0).height
            verify(rowHeight > 0)
            verify(view.contentHeight - view.height >= lines * rowHeight, "內容夠捲 " + lines + " 列")
            fuzzyCompare(notch(view), lines * rowHeight, 0.5)
        }

        // 專輯頁的三種檢視、所有曲目、藝人、資料夾：每個可捲動的清單都放了 WheelScroll
        function test_every_list_has_wheel_scroll() {
            const expected = { grid: "albumGrid", list: "albumList", detail: "albumDetailList" }
            const names = []
            for (const mode of ["grid", "list", "detail"]) {
                LibraryAlbums.view = mode
                // 檢視由 Loader 換：等新的那個建好（舊的會被銷毀，當下就檢查）
                tryVerify(() => root.find(albumsPage, expected[mode]) !== null, 2000, mode)
                for (const view of root.flickables(albumsPage, [])) {
                    names.push(view.objectName)
                    verify(root.wheelOf(view) !== null, "專輯頁（" + mode + "）" + view.objectName + " 有 WheelScroll")
                }
            }
            for (const page of [tracksPage, artistsPage, foldersPage]) {
                for (const view of root.flickables(page, [])) {
                    names.push(view.objectName)
                    verify(root.wheelOf(view) !== null, page.objectName + " " + view.objectName + " 有 WheelScroll")
                }
            }
            for (const name of ["albumGrid", "albumList", "albumDetailList", "libraryTrackList", "artistList",
                                "folderTree", "folderTrackList"])
                verify(names.indexOf(name) >= 0, "找到 " + name + "（找到的：" + names.join("、") + "）")
        }
    }
}
