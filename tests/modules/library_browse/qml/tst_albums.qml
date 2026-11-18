import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 專輯頁（S-03，T-22）的鍵盤走查：Tab 進封面牆、方向鍵移動、Enter 開專輯詳細（還沒登記時不做事）、
// 封面的播放鍵播放整張專輯、工具列的檢視方式（方向鍵）與排序、篩選無結果的狀態。
Item {
    id: root
    width: 1100
    height: 720

    function focusItem() {
        return root.Window.activeFocusItem
    }

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

    Button { id: start; text: "起點" }

    Loader {
        id: toolbarLoader
        y: 40
        width: 600
        height: Theme.headerHeight
        sourceComponent: page.toolbar
    }

    AlbumsPage {
        id: page
        y: 100
        width: 1000
        height: 600
    }

    TestCase {
        name: "AlbumsPage"
        when: windowShown

        function init() {
            LibrarySearch.clearFilters()
            LibraryAlbums.view = "grid"
            LibraryAlbums.sort = "added"
            fakePlayback.reset(0, -1)
            tryVerify(() => LibraryAlbums.loaded && LibraryAlbums.count === 8, 5000)
            // 封面牆會記住上次的目前項目（Tab 回來時停在原處）；每個測試從「還沒選過」開始，
            // 不受前一個測試（例：test_enter_opens_album_page 設的 2）影響
            const g = root.find(page, "albumGrid")
            if (g)
                g.currentIndex = -1
            start.forceActiveFocus()
        }

        function grid() {
            const g = root.find(page, "albumGrid")
            verify(g, "封面牆")
            return g
        }

        function test_count_and_sidebar_order() {
            compare(page.countText, "8 張專輯")
            compare(page.statusText, "8 張專輯")
            // 加入時間：新的在前
            compare(LibraryAlbums.albumIdAt(0), 8)
        }

        function test_keyboard_grid() {
            const g = grid()
            // Tab 順序：檢視方式 → 排序 → 篩選列的 Chip → 封面牆
            for (let i = 0; i < 10 && !g.activeFocus; ++i)
                keyClick(Qt.Key_Tab)
            verify(g.activeFocus, "Tab 走得到封面牆")
            compare(g.currentIndex, 0)
            keyClick(Qt.Key_Right)
            compare(g.currentIndex, 1)
            keyClick(Qt.Key_Down)
            compare(g.currentIndex, 1 + g.columns)
            // 專輯詳細頁還沒登記：Enter 不做事
            const before = Navigator.currentPage
            keyClick(Qt.Key_Return)
            compare(Navigator.currentPage, before)
        }

        function test_enter_opens_album_page() {
            pageProbe.addAlbumPage()
            const g = grid()
            g.forceActiveFocus()
            g.currentIndex = 2
            keyClick(Qt.Key_Return)
            compare(Navigator.currentPage, "library/album")
            compare(Number(Navigator.params.albumId), Number(LibraryAlbums.albumIdAt(2)))
            Navigator.back()
        }

        function test_play_button_plays_whole_album() {
            const g = grid()
            g.forceActiveFocus()
            g.currentIndex = 0
            // 選到的格子（鍵盤）也顯示播放鍵
            const button = root.find(g.currentItem, "albumPlay")
            verify(button)
            tryVerify(() => button.visible)
            mouseClick(button)
            compare(fakePlayback.calls, ["playFrom 0"])
            compare(fakePlayback.paths, ["C:/t/8-1.flac", "C:/t/8-2.flac"])
        }

        function test_view_switch_with_keyboard() {
            const views = root.find(toolbarLoader.item, "albumsView")
            verify(views)
            // Tab 停在選取的那格：起點 → 工具列的檢視方式
            keyClick(Qt.Key_Tab)
            verify(focusItem() && focusItem().checked, "焦點在選取的檢視")
            keyClick(Qt.Key_Right)
            compare(LibraryAlbums.view, "list")
            tryVerify(() => root.find(page, "albumList") !== null)
            keyClick(Qt.Key_Right)
            compare(LibraryAlbums.view, "detail")
            tryVerify(() => root.find(page, "albumDetailList") !== null)
            // 詳細檢視：Tab 進清單、下鍵換專輯
            const detail = root.find(page, "albumDetailList")
            detail.forceActiveFocus()
            compare(detail.currentIndex, 0)
            keyClick(Qt.Key_Down)
            compare(detail.currentIndex, 1)
        }

        function test_list_view_keyboard() {
            LibraryAlbums.view = "list"
            const list = root.find(page, "albumList")
            verify(list)
            list.forceActiveFocus()
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 2)
        }

        function test_sort_with_keyboard() {
            const sort = root.find(toolbarLoader.item, "albumsSort")
            verify(sort)
            sort.forceActiveFocus()
            keyClick(Qt.Key_Down)
            compare(LibraryAlbums.sort, "title")
            compare(LibraryAlbums.albumIdAt(0), 1)
        }

        function test_filter_no_match() {
            LibrarySearch.format = "WAV"
            compare(LibraryAlbums.count, 1)
            compare(page.countText, "1 張專輯")
            LibrarySearch.sampleRate = 44100
            compare(LibraryAlbums.count, 0)
            const empty = root.find(page, "albumsNoMatch")
            verify(empty && empty.visible)
            compare(empty.title, "沒有符合的專輯")
            compare(empty.actionText, "清除篩選")
            LibrarySearch.text = "搜尋字串不影響專輯頁"
            compare(LibraryAlbums.count, 0)
            LibrarySearch.clear()
            compare(LibraryAlbums.count, 8)
        }
    }
}
