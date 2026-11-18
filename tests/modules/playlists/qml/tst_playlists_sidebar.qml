import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Pl

// 側欄播放清單區與右鍵「加入播放清單 ▸」（T-50，US-11 AC-1、3、4；畫面規格 S-02 側欄、S-08）：
// 鍵盤新增與改名（Enter 確定、Esc 取消、F2）、Delete 與右鍵選單的刪除要先確認（文案照 S-08）、
// 曲目拖到某一列＝加入、拖進這一區時出現「拖到這裡建立新播放清單」、拖到那裡＝建立並加入、
// 曲目右鍵選單用鍵盤走到「加入播放清單 ▸」加入。
Item {
    id: root
    width: 900
    height: 600

    Button { id: start; text: "起點" }

    PlaylistsSection {
        id: section
        x: 0
        y: 40
        width: Theme.sidebarWidth
    }

    // 音樂庫清單的替身：拖曳兩首曲目
    TrackDragArea {
        id: source
        x: 500
        y: 300
        width: 200
        height: 40
        tracks: [{ path: "D:/音樂/a.flac", subsong: 0 }, { path: "D:/音樂/b.flac", subsong: 0 }]
        Rectangle { anchors.fill: parent; color: Theme.surfaceRaised }
    }

    TrackMenu { id: trackMenu }
    Button { id: menuAnchor; x: 500; y: 100; text: "選單" }

    SignalSpy { id: confirmSpy; target: coreDialogs; signalName: "requested" }

    TestCase {
        name: "PlaylistsSidebar"
        when: windowShown

        function init() {
            plProbe.resetPlaylists()
            confirmSpy.clear()
            start.forceActiveFocus()
        }

        function rowOf(id) {
            return section.rowFor(id)
        }

        function playlistNamed(name) {
            const list = Playlists.playlists
            for (let i = 0; i < list.length; ++i) {
                if (list[i].name === name)
                    return list[i]
            }
            return null
        }

        function drag(toItem, tx, ty) {
            const from = source.mapToItem(root, 100, 20)
            const to = toItem.mapToItem(root, tx, ty)
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 12; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 12, from.y + (to.y - from.y) * i / 12)
            return to
        }

        function test_add_and_rename_by_keyboard() {
            const add = findChild(section, "addPlaylistButton")
            add.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(Playlists.playlists.length, 1)
            const id = Playlists.playlists[0].id
            compare(Playlists.playlists[0].name, "新播放清單")
            compare(Playlists.editingId, id)
            let row = rowOf(id)
            tryVerify(() => row.nameField.input.activeFocus)
            // 預設名稱已全選：直接打字取代
            keyClick(Qt.Key_C)
            keyClick(Qt.Key_D)
            keyClick(Qt.Key_Return)
            compare(Playlists.playlists[0].name, "cd")
            compare(Playlists.editingId, 0)
            // 改名後清單重建，焦點回到那一列
            tryVerify(() => rowOf(id).sidebarItem.activeFocus)
            row = rowOf(id)

            // F2 改名、Esc 取消不改
            keyClick(Qt.Key_F2)
            tryVerify(() => row.nameField.input.activeFocus)
            keyClick(Qt.Key_X)
            keyClick(Qt.Key_Escape)
            compare(Playlists.editingId, 0)
            compare(Playlists.playlists[0].name, "cd")
            tryVerify(() => row.sidebarItem.activeFocus)
        }

        function test_delete_asks_first() {
            const id = Playlists.createPlaylist()
            verify(Playlists.rename(id, "通勤"))
            const row = rowOf(id)
            row.sidebarItem.forceActiveFocus()
            keyClick(Qt.Key_Delete)
            compare(confirmSpy.count, 1)
            const options = confirmSpy.signalArguments[0][2]
            compare(confirmSpy.signalArguments[0][1], "confirm")
            compare(options.title, "刪除播放清單「通勤」")
            compare(options.message, "只刪除播放清單，不會刪除任何音樂檔。")
            compare(options.confirmText, "刪除播放清單")
            coreDialogs.respond(confirmSpy.signalArguments[0][0], false)
            compare(Playlists.playlists.length, 1)

            // 右鍵選單的「刪除播放清單」也一樣先問
            mouseClick(row.sidebarItem, 20, 10, Qt.RightButton)
            const menu = findChild(section, "playlistItemMenu")
            tryVerify(() => menu.opened)
            compare(menu.itemAt(0).text, "播放")
            compare(menu.itemAt(1).text, "重新命名")
            // 「匯出」在 T-54 加在「重新命名」後面（S-08）
            compare(menu.itemAt(2).text, "匯出")
            compare(menu.itemAt(3).text, "刪除播放清單")
            menu.itemAt(3).triggered()
            menu.close()
            compare(confirmSpy.count, 2)
            coreDialogs.respond(confirmSpy.signalArguments[1][0], true)
            compare(Playlists.playlists.length, 0)
        }

        function test_menu_key_opens_item_menu() {
            const id = Playlists.createPlaylist()
            verify(Playlists.rename(id, "鍵盤"))
            const row = rowOf(id)
            row.sidebarItem.forceActiveFocus()
            keyClick(Qt.Key_F10, Qt.ShiftModifier)
            const menu = findChild(section, "playlistItemMenu")
            tryVerify(() => menu.opened)
            // 空的播放清單不能播放：停在「重新命名」（T-55）
            verify(!menu.itemAt(0).enabled)
            compare(menu.currentIndex, 1)
            keyClick(Qt.Key_Return)
            tryVerify(() => !menu.opened)
            compare(Playlists.editingId, id)
            tryVerify(() => row.nameField.input.activeFocus)
            keyClick(Qt.Key_Escape)
        }

        function test_drag_to_playlist_adds() {
            const id = Playlists.createPlaylist()
            verify(Playlists.rename(id, "通勤"))
            const row = rowOf(id)
            // 清單重讀後新的一列要等 Column 排好位置（下一次 polish）
            tryVerify(() => row.y > 0)
            const to = drag(row, 40, row.height / 2)
            tryVerify(() => row.sidebarItem.dropTarget)
            mouseRelease(root, to.x, to.y)
            tryVerify(() => Playlists.info(id).count === 2)
            compare(coreNotifier.current.text, "已加入 2 首到「通勤」")
            verify(!rowOf(id).sidebarItem.dropTarget)
            // 拖完虛線框收起
            tryVerify(() => !findChild(section, "newPlaylistZone").visible)
        }

        function test_drag_to_empty_slot_creates() {
            const zone = findChild(section, "newPlaylistZone")
            verify(!zone.visible)
            // 拖進這一區（標籤列）時才出現虛線框，再拖到框裡
            const from = source.mapToItem(root, 100, 20)
            const header = section.mapToItem(root, 40, 10)
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 12; ++i)
                mouseMove(root, from.x + (header.x - from.x) * i / 12, from.y + (header.y - from.y) * i / 12)
            tryVerify(() => zone.visible && zone.y > 0)
            const target = zone.mapToItem(root, zone.width / 2, zone.height / 2)
            mouseMove(root, target.x, target.y)
            mouseRelease(root, target.x, target.y)
            tryCompare(Playlists.playlists, "length", 1)
            compare(Playlists.playlists[0].count, 2)
            compare(Playlists.playlists[0].name, "新播放清單")
            compare(Playlists.editingId, Playlists.playlists[0].id)
            tryVerify(() => !zone.visible)
            keyClick(Qt.Key_Escape)
        }

        function test_track_menu_add_to_playlist_by_keyboard() {
            const first = Playlists.createPlaylist()
            verify(Playlists.rename(first, "通勤"))
            const second = Playlists.createPlaylist()
            verify(Playlists.rename(second, "深夜"))
            const json = TrackDragCodec.encode([{ path: "D:/音樂/c.flac", subsong: 0 }], "", 1)
            menuAnchor.forceActiveFocus()
            trackMenu.popupFrom(json, 1, menuAnchor)
            tryVerify(() => trackMenu.opened)
            // 「播放」「接著播放」「加到佇列最後」之後第一個區塊就是「加入播放清單」
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(trackMenu.currentIndex, 3)
            const sub = trackMenu.menuAt(3)
            verify(sub !== null)
            compare(sub.title, "加入播放清單")
            compare(sub.count, 3)
            compare(sub.itemAt(0).text, "通勤")
            compare(sub.itemAt(1).text, "深夜")
            compare(sub.itemAt(2).text, "新播放清單…")
            keyClick(Qt.Key_Right)
            tryVerify(() => sub.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            // 子選單裡的項目觸發後整個選單關掉
            tryVerify(() => !trackMenu.visible)
            compare(Playlists.info(second).count, 1)
            compare(Playlists.info(first).count, 0)

            // 「新播放清單…」：建立並加入
            menuAnchor.forceActiveFocus()
            trackMenu.popupFrom(json, 1, menuAnchor)
            tryVerify(() => trackMenu.opened)
            trackMenu.menuAt(3).itemAt(2).triggered()
            tryCompare(Playlists.playlists, "length", 3)
            compare(Playlists.playlists[2].count, 1)
            trackMenu.close()
            Playlists.editingId = 0
        }

        function test_query_only_keeps_submenu() {
            // 只帶查詢條件（超過 5,000 首）：子選單照樣可用，曲目由 Playlists 在背景取
            Playlists.createPlaylist()
            const json = TrackDragCodec.encode([], "{\"lib\":1,\"text\":\"bach\"}", 6000)
            trackMenu.popupFrom(json, 6000, menuAnchor)
            tryVerify(() => trackMenu.opened)
            verify(trackMenu.menuAt(3).enabled)
            compare(trackMenu.menuAt(3).count, 2)
            trackMenu.close()
            Playlists.editingId = 0
        }
    }
}
