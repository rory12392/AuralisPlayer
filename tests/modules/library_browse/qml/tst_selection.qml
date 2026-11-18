import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 清單的選取、右鍵選單與拖放（T-25，US-10 AC-5、US-11 AC-4 的來源端；DESIGN §7 清單的鍵盤操作）：
// 曲目清單（LibraryTrackList，所有曲目與資料夾頁共用）的 Shift＋上下鍵、Ctrl＋點擊、Ctrl+A、
// Shift+Enter、Ctrl+Enter、選單鍵與狀態列說明；右鍵選單的「前往專輯」「前往藝人」；拖到佇列
// （TrackDropArea）收到的是選取的曲目；專輯牆以整張專輯為單位；藝人頁的開頁參數 {artist}。
// 檔名排在 tst_detail_page 之後：那邊已經登記了真的專輯詳細頁。
Item {
    id: root
    width: 1100
    height: 760

    property var lastPayload: null

    function itemByText(menu, text) {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.text === text)
                return item
        }
        return null
    }

    Button { id: start; text: "起點" }

    TracksPage {
        id: tracksPage
        y: 40
        width: 900
        height: 400
    }

    // 佇列面板的替身：只收拖放
    Rectangle {
        id: queue
        x: 920
        y: 40
        width: 160
        height: 400
        color: Theme.surface
        TrackDropArea {
            anchors.fill: parent
            onTracksDropped: payload => root.lastPayload = payload
        }
    }

    AlbumGrid {
        id: grid
        y: 460
        width: 900
        height: 290
    }

    TestCase {
        name: "TrackListSelection"
        when: windowShown

        // TracksPage 裡的 LibraryTrackList（清單本身的 objectName 是 libraryTrackList）
        property var list: null

        function initTestCase() {
            pageProbe.addRealAlbumPage()
            list = findChild(tracksPage, "libraryTrackList").parent
        }

        function init() {
            fakePlayback.reset(0, -1)
            LibrarySearch.clear()
            // busy 含去抖：條件改了以後要等新查詢回來，否則測試中途模型重設、選取被清掉
            tryVerify(() => !LibrarySearch.busy && LibrarySearch.complete && LibrarySearch.count === 16, 5000)
            list.selection.clear()
            list.currentIndex = -1
            start.forceActiveFocus()
        }

        function focusList() {
            const view = findChild(list, "libraryTrackList")
            view.forceActiveFocus()
            return view
        }

        function test_keyboard_selection_and_status() {
            focusList()
            compare(tracksPage.statusTextLeft, "")
            keyClick(Qt.Key_Down)
            compare(list.selection.count, 1)
            compare(list.currentIndex, 0)
            compare(tracksPage.statusTextLeft,
                    "已選取 1 首。拖到左側播放清單即可加入；右鍵可選「接著播放」或「加到佇列最後」。")
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            compare(list.selection.count, 3)
            compare(list.currentIndex, 2)
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            compare(list.selection.count, 2)
            // 沒有 Shift：只選目前列
            keyClick(Qt.Key_Down)
            compare(list.selection.count, 1)
            compare(list.currentIndex, 2)

            keyClick(Qt.Key_A, Qt.ControlModifier)
            compare(list.selection.count, 16)
            verify(tracksPage.statusTextLeft.startsWith("已選取 16 首。"))
        }

        function test_enter_variants() {
            focusList()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Return, Qt.ShiftModifier)
            compare(fakePlayback.calls, ["enqueueNext 2"])
            keyClick(Qt.Key_Return, Qt.ControlModifier)
            compare(fakePlayback.calls, ["enqueueNext 2", "enqueueLast 2"])
            // Enter：從目前這首播放（整份清單，不只選取的）
            keyClick(Qt.Key_Return)
            compare(fakePlayback.calls[2], "playFrom 1")
            compare(fakePlayback.paths.length, 16)
        }

        function test_ctrl_and_shift_click() {
            const view = focusList()
            const row0 = view.itemAtIndex(0)
            const row3 = view.itemAtIndex(3)
            mouseClick(row0, 200, row0.height / 2)
            compare(list.selection.count, 1)
            mouseClick(row3, 200, row3.height / 2, Qt.LeftButton, Qt.ControlModifier)
            compare(list.selection.count, 2)
            verify(list.selection.isSelected(3))
            mouseClick(row0, 200, row0.height / 2, Qt.LeftButton, Qt.ShiftModifier)
            compare(list.selection.count, 4) // 錨點在 3，選到 0
        }

        function test_menu_key_and_go_to_album() {
            const view = focusList()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Menu)
            const menu = list.menu
            tryVerify(() => menu.opened)
            compare(menu.trackCount, 1)
            const goAlbum = root.itemByText(menu, "前往專輯")
            const goArtist = root.itemByText(menu, "前往藝人")
            verify(goAlbum !== null && goArtist !== null)
            verify(goAlbum.enabled)
            verify(goArtist.enabled)
            // 「前往專輯」排在核心項目之後、「開啟檔案位置」之前
            compare(menu.itemAt(3).text, "前往專輯")
            goAlbum.triggered()
            menu.close()
            compare(Navigator.currentPage, "library/album")
            compare(Number(Navigator.params.albumId), 1)

            // 兩首時「前往」不能用
            view.forceActiveFocus()
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_F10, Qt.ShiftModifier)
            tryVerify(() => menu.opened)
            compare(menu.trackCount, 2)
            verify(!root.itemByText(menu, "前往專輯").enabled)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !menu.visible)
        }

        function test_right_click_keeps_selection() {
            const view = focusList()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            const row1 = view.itemAtIndex(1)
            mouseClick(row1, 200, row1.height / 2, Qt.RightButton)
            // 在游標位置開（Menu.popup() 不帶位置）在 offscreen 平台不會開：這裡驗證選單已經拿到
            // 這次的內容（popupAt 的 prepare），實際開啟在主程式驗證（回報檔）
            compare(list.currentIndex, 1)
            compare(list.menu.trackCount, 2)
            compare(TrackDragCodec.decode(list.menu.payloadJson).tracks.length, 2)
            list.menu.close()
            // 沒選取的列：改成只選它
            const row5 = view.itemAtIndex(5)
            mouseClick(row5, 200, row5.height / 2, Qt.RightButton)
            compare(list.menu.trackCount, 1)
            compare(TrackDragCodec.decode(list.menu.payloadJson).tracks[0].path, "C:/t/3-2.flac")
            verify(list.selection.isSelected(5))
            compare(list.selection.count, 1)
            list.menu.close()
        }

        function test_drag_selection_to_queue() {
            const view = focusList()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            root.lastPayload = null
            const row = view.itemAtIndex(1)
            const from = row.mapToItem(root, 200, row.height / 2)
            const to = queue.mapToItem(root, 80, 200)
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 10; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10)
            mouseRelease(root, to.x, to.y)
            tryVerify(() => root.lastPayload !== null)
            compare(root.lastPayload.count, 3)
            compare(root.lastPayload.tracks.length, 3)
            compare(root.lastPayload.tracks[0].path, "C:/t/1-1.flac")
            compare(root.lastPayload.tracks[2].path, "C:/t/2-1.flac")
        }
    }

    TestCase {
        name: "AlbumGridSelection"
        when: windowShown

        function init() {
            LibraryAlbums.load()
            tryVerify(() => LibraryAlbums.loaded && grid.count === 8, 5000)
            grid.selection.clear()
            grid.currentIndex = -1
            fakePlayback.reset(0, -1)
        }

        function test_whole_albums() {
            grid.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(grid.selection.count, 1)
            compare(grid.selection.trackCount, 2)
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            compare(grid.selection.count, 2)
            compare(grid.selection.trackCount, 4)
            verify(grid.selectionText.startsWith("已選取 4 首。"))
            keyClick(Qt.Key_Return, Qt.ControlModifier)
            compare(fakePlayback.calls, ["enqueueLast 4"])
            keyClick(Qt.Key_A, Qt.ControlModifier)
            compare(grid.selection.trackCount, 16)
        }

        function test_drag_album_to_queue() {
            root.lastPayload = null
            const cell = grid.itemAtIndex(2)
            const from = cell.mapToItem(root, 40, 40)
            const to = queue.mapToItem(root, 80, 200)
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 10; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10)
            mouseRelease(root, to.x, to.y)
            tryVerify(() => root.lastPayload !== null)
            compare(root.lastPayload.tracks.length, 2)
            verify(grid.selection.isSelected(2))
        }
    }

    TestCase {
        name: "ArtistsParam"
        when: windowShown

        function initTestCase() {
            pageProbe.addArtistsPage()
        }

        function test_open_with_artist() {
            LibraryArtists.load()
            tryVerify(() => LibraryArtists.loaded, 5000)
            const page = createTemporaryObject(Qt.createComponent("qrc:/qt/qml/Auralis/Lib/ArtistsPage.qml"), root,
                                               { width: 900, height: 400 })
            verify(page !== null)
            verify(Navigator.open("library/artists", { artist: "藝人 3" }))
            tryCompare(LibraryArtists, "selectedArtist", "藝人 3")
            verify(Navigator.open("library/artists", { artist: "沒有這位" }))
            tryCompare(LibraryArtists, "selectedRow", 0)
        }

        // 篩選列（fix-m02-review）：選項讀取失敗時顯示原因與「重試」，重試成功後消失
        function test_filter_options_error_and_retry() {
            const error = findChild(tracksPage, "filterOptionsError")
            const retry = findChild(tracksPage, "filterOptionsRetry")
            verify(error !== null && retry !== null)
            verify(!error.visible)
            pageProbe.setTracksFail(true)
            LibrarySearch.loadOptions()
            tryVerify(() => error.visible, 5000)
            compare(error.variant, "signal")
            verify(error.text.length > 0)
            verify(retry.visible)
            pageProbe.setTracksFail(false)
            mouseClick(retry)
            tryVerify(() => !error.visible && !retry.visible, 5000)
            verify(LibrarySearch.optionsLoaded)
            verify(findChild(tracksPage, "filterFormat").enabled)
        }
    }
}
