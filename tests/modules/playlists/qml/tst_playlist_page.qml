import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Pl

// 播放清單頁（T-55，US-11 AC-1；畫面規格 S-08）：版面與空狀態、找不到檔案的列、
// 鍵盤（選取、Ctrl＋上下鍵排序、Delete 移除與「復原」、Enter 播放、F2 改名）、
// 拖曳把手排序（放開時一次寫入）、從音樂庫拖到清單中間＝插在那裡、右鍵選單。
Item {
    id: root
    width: 1100
    height: 700

    PlaylistPage {
        id: page
        x: 0
        y: 0
        width: 900
        height: 600
    }

    // 音樂庫清單的替身：拖一首曲目
    TrackDragArea {
        id: source
        x: 920
        y: 300
        width: 160
        height: 40
        tracks: [{ path: "D:/音樂/新.flac", subsong: 0 }]
        Rectangle { anchors.fill: parent; color: Theme.surfaceRaised }
    }

    TestCase {
        name: "PlaylistPage"
        when: windowShown

        property var list: null

        function open(names) {
            plProbe.resetPlaylists()
            const id = plProbe.makePlaylist("通勤", names)
            Navigator.open("playlist", { id: id })
            tryVerify(() => PlaylistTracks.playlistId === id)
            // 清空後資料庫會重用同一個 id，頁面看不出換了內容：清掉再設回去，等重讀完
            PlaylistTracks.playlistId = 0
            PlaylistTracks.playlistId = id
            tryVerify(() => PlaylistTracks.status === "ready" && PlaylistTracks.count === names.length)
            list = findChild(page, "playlistTrackList")
            verify(list)
            return id
        }

        function rowY(row) {
            return list.mapToItem(root, 0, row * Theme.rowHeight + Theme.rowHeight / 2).y
        }

        function test_layout_and_empty_state() {
            open([])
            compare(findChild(page, "playlistName").text, "通勤")
            compare(findChild(page, "playlistSummary").text, "0 首　0:00")
            verify(findChild(page, "playlistEmpty").visible)
            // 空狀態的「匯入播放清單」（T-51）開開檔對話框
            compare(findChild(page, "playlistEmpty").actionText, "匯入播放清單")
            verify(!findChild(page, "playPlaylistButton").enabled)
        }

        function test_missing_rows_use_file_name() {
            open(["a", "b"])
            compare(findChild(page, "playlistSummary").text, "2 首　0:00")
            verify(!findChild(page, "playlistEmpty").visible)
            const row = findChild(list.itemAtIndex(0), "playlistTrackRow")
            verify(row)
            compare(row.title, "a")
            verify(row.missing)
            compare(row.statusText, "找不到檔案")
        }

        // T-52（S-08）：有找不到的檔案時頂部一行「<m> 首找不到檔案」＋「重新對應資料夾」
        // （對整份播放清單開 S-09 的對應區）；檔案在磁碟上但不在音樂庫的列標「不在音樂庫」
        function test_missing_bar_and_not_in_library() {
            const id = open(["a", "b"])
            const bar = findChild(page, "playlistMissingBar")
            verify(bar.visible)
            compare(findChild(page, "playlistMissingText").text, "2 首找不到檔案")
            const button = findChild(page, "playlistRemapButton")
            compare(button.text, "重新對應資料夾")
            // S-08 頂部提示的截圖（畫面審查用）
            waitForRendering(page)
            const shot = grabImage(page)
            shot.save(plProbe.shotPath("s08-missing-bar.png"))
            mouseClick(button)
            tryVerify(() => PlaylistImport.result.mode === "playlist")
            compare(PlaylistImport.result.id, id)
            compare(PlaylistImport.result.missing, 2)
            PlaylistImport.dismiss()

            // 磁碟上有、音樂庫沒有：不算找不到，文字照常、標「不在音樂庫」
            const file = plProbe.makeFile("沒加入/c.flac")
            verify(Playlists.addTracks(id, JSON.stringify({ tracks: [{ path: file, subsong: 0 }] })))
            tryVerify(() => PlaylistTracks.count === 3)
            const row = findChild(list.itemAtIndex(2), "playlistTrackRow")
            verify(row)
            verify(!row.missing)
            compare(row.statusText, "不在音樂庫")
            compare(findChild(page, "playlistMissingText").text, "2 首找不到檔案")
        }

        function test_keyboard_select_move_remove_undo_play() {
            const id = open(["a", "b", "c", "d"])
            list.forceActiveFocus()
            list.currentIndex = 0
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            verify(PlaylistTracks.isSelected(1))
            // Ctrl＋下：b 往下移一格，目前列跟著走
            keyClick(Qt.Key_Down, Qt.ControlModifier)
            compare(plProbe.storedNames(id), ["a", "c", "b", "d"])
            compare(list.currentIndex, 2)
            // Shift＋下 複選 b、d，Delete 移除，通知帶「復原」
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            compare(PlaylistTracks.selectedCount, 2)
            keyClick(Qt.Key_Delete)
            compare(plProbe.storedNames(id), ["a", "c"])
            compare(PlaylistTracks.count, 2)
            compare(coreNotifier.current.text, "已移除 2 首")
            compare(coreNotifier.current.actionText, "復原")
            coreNotifier.triggerAction(coreNotifier.current.id)
            compare(plProbe.storedNames(id), ["a", "c", "b", "d"])
            tryVerify(() => PlaylistTracks.count === 4)
            // Enter 從目前這首播放整個清單
            list.forceActiveFocus()
            list.currentIndex = 2
            keyClick(Qt.Key_Return)
            compare(plPlayback.calls[plPlayback.calls.length - 1], "playFrom 2")
            compare(plPlayback.paths.length, 4)
            // Ctrl+A 全選
            keyClick(Qt.Key_A, Qt.ControlModifier)
            compare(PlaylistTracks.selectedCount, 4)
        }

        function test_f2_renames() {
            open(["a"])
            list.forceActiveFocus()
            keyClick(Qt.Key_F2)
            const field = findChild(page, "playlistNameField")
            tryVerify(() => field.input.activeFocus)
            keyClick(Qt.Key_X)
            keyClick(Qt.Key_Return)
            tryVerify(() => findChild(page, "playlistName").text === "x")
            tryVerify(() => list.activeFocus)
        }

        function test_handle_drag_reorders_on_release() {
            const id = open(["a", "b", "c", "d"])
            const handle = findChild(page, "playlistReorderHandle")
            verify(handle.visible)
            const x = handle.mapToItem(root, handle.width / 2, 0).x
            // 把 a 拖到 c 的下半部＝放在 c 後面
            mousePress(root, x, rowY(0))
            for (let i = 1; i <= 10; ++i)
                mouseMove(root, x, rowY(0) + (rowY(2) + Theme.rowHeight / 4 - rowY(0)) * i / 10)
            // 放開前不寫入
            compare(plProbe.storedNames(id), ["a", "b", "c", "d"])
            mouseRelease(root, x, rowY(2) + Theme.rowHeight / 4)
            compare(plProbe.storedNames(id), ["b", "c", "a", "d"])
            compare(list.currentIndex, 2)
        }

        function test_drop_from_library_inserts_at_position() {
            const id = open(["a", "b", "c"])
            const from = source.mapToItem(root, 80, 20)
            const to = { x: 400, y: rowY(1) - Theme.rowHeight / 4 }
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 12; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 12, from.y + (to.y - from.y) * i / 12)
            mouseRelease(root, to.x, to.y)
            tryVerify(() => plProbe.storedNames(id).length === 4)
            compare(plProbe.storedNames(id), ["a", "新", "b", "c"])
            tryVerify(() => PlaylistTracks.count === 4)
        }

        // M-04 審查（附帶 B）：拖曳小卡的數字是實際會拖出去的曲目數（找不到的不算），
        // 全部找不到時是 0（不能拖）
        function test_drag_count_skips_missing_rows() {
            plProbe.resetPlaylists()
            const local = plProbe.makeFile("拖曳/a.flac")
            PlaylistImport.importFiles([plProbe.writePlaylist("拖曳.m3u8",
                "#EXTM3U\n\\\\evil-host\\share\\x.flac\n" + local + "\n")])
            tryVerify(() => PlaylistImport.result.id > 0)
            const id = PlaylistImport.result.id
            PlaylistImport.dismiss()
            Navigator.open("playlist", { id: id })
            PlaylistTracks.playlistId = 0
            PlaylistTracks.playlistId = id
            tryVerify(() => PlaylistTracks.status === "ready" && PlaylistTracks.count === 2)
            list = findChild(page, "playlistTrackList")
            const blocked = list.itemAtIndex(0)
            const playable = list.itemAtIndex(1)
            compare(blocked.objectName, "playlistTrackDrag")
            PlaylistTracks.selectAll()
            compare(PlaylistTracks.selectedCount, 2)
            compare(blocked.count, 1)
            compare(playable.count, 1)
            PlaylistTracks.click(1)
            compare(blocked.count, 0)
            PlaylistTracks.click(0)
            compare(blocked.count, 0)
        }

        function test_right_click_opens_track_menu() {
            open(["a", "b"])
            const row = list.itemAtIndex(1)
            mouseClick(row, 200, Theme.rowHeight / 2, Qt.RightButton)
            const menu = findChild(page, "playlistTrackList").parent.menu
            tryVerify(() => menu.opened)
            verify(PlaylistTracks.isSelected(1))
            compare(PlaylistTracks.selectedCount, 1)
            menu.close()
            tryVerify(() => !menu.visible)
        }
    }
}
