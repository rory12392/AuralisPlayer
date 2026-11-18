import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Pl

// 匯出播放清單（T-54，US-14；畫面規格 S-10）：側欄右鍵「匯出」與播放清單頁的「匯出」開同一個對話框
// （標題「匯出「<名稱>」」、「路徑」絕對／相對＋一行說明、「選擇位置並匯出」／「取消」）；
// 選擇位置開系統的儲存對話框（預設檔名「<名稱>.m3u8」）；已存在時先確認「覆蓋 <檔名>」，取消就不寫。
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

    PlaylistHeader {
        id: header
        x: Theme.sidebarWidth + 20
        y: 40
        width: 600
        name: "通勤"
    }

    SignalSpy { id: dialogSpy; target: coreDialogs; signalName: "requested" }

    TestCase {
        name: "PlaylistExport"
        when: windowShown

        function init() {
            plProbe.resetPlaylists()
            dialogSpy.clear()
            start.forceActiveFocus()
        }

        function exportDialog() {
            return findChild(section, "playlistExportDialog")
        }

        function rowOf(id) {
            return findChild(section, "playlist-" + id)
        }

        function test_sidebar_menu_opens_dialog_and_exports() {
            const id = plProbe.makePlaylist("通勤", ["a", "b"])
            const row = rowOf(id)
            tryVerify(() => row !== null)
            mouseClick(row.sidebarItem, 20, 10, Qt.RightButton)
            const menu = findChild(section, "playlistItemMenu")
            tryVerify(() => menu.opened)
            compare(menu.itemAt(2).text, "匯出")
            menu.itemAt(2).triggered()
            menu.close()

            const dialog = exportDialog()
            tryVerify(() => dialog.opened)
            compare(dialog.title, "匯出「通勤」")
            compare(dialog.confirmText, "選擇位置並匯出")
            compare(dialog.cancelText, "取消")
            const mode = findChild(dialog.contentItem, "exportPathMode")
            const hint = findChild(dialog.contentItem, "exportPathHint")
            compare(mode.label, "路徑")
            compare(mode.currentIndex, 0)
            compare(hint.text, "每一首寫完整的路徑（含磁碟機代號）")
            mode.activated(1)
            compare(mode.currentIndex, 1)
            compare(hint.text, "相對於匯出檔所在的資料夾；不同磁碟機的曲目仍寫完整路徑")
            // S-10 的截圖（畫面審查用；對話框本身的那一塊）
            wait(200)
            waitForRendering(dialog.contentItem)
            const shot = grabImage(dialog.background.parent)
            verify(shot.width > 0)
            shot.save(plProbe.tempPath("s10-export.png"))

            dialog.accept()
            tryVerify(() => !dialog.opened)
            compare(dialogSpy.count, 1)
            compare(dialogSpy.signalArguments[0][1], "saveFile")
            const options = dialogSpy.signalArguments[0][2]
            // 預設檔名接在起始資料夾後面（F-3：沒匯出過時是「文件」，不是系統記得的位置）
            verify(decodeURIComponent(String(options.suggestedName)).endsWith("/通勤.m3u8")
                   || String(options.suggestedName).endsWith("\\通勤.m3u8"),
                   String(options.suggestedName))
            compare(options.nameFilters, ["M3U8 播放清單 (*.m3u8)"])
            const target = plProbe.quickDir() + "\\通勤.m3u8"
            coreDialogs.respond(dialogSpy.signalArguments[0][0], [target])
            // 不存在：不確認直接寫（沒有音樂庫，曲目都是 #EXTINF:-1,<檔名>）
            compare(dialogSpy.count, 1)
            const drive = plProbe.trackDrive()
            compare(plProbe.readText(target),
                    "#EXTM3U\r\n#EXTINF:-1,a.flac\r\n" + drive + "\\音樂\\a.flac\r\n#EXTINF:-1,b.flac\r\n"
                    + drive + "\\音樂\\b.flac\r\n")
            compare(PlaylistExport.relative, true)
        }

        function test_existing_target_asks_and_cancel_keeps_file() {
            const id = plProbe.makePlaylist("深夜", ["c"])
            const target = plProbe.writePlaylist("深夜.m3u8", "原本的內容")
            header.playlistId = id
            mouseClick(findChild(header, "exportPlaylistButton"))
            const dialog = exportDialog()
            tryVerify(() => dialog.opened)
            compare(dialog.title, "匯出「深夜」")
            dialog.accept()
            coreDialogs.respond(dialogSpy.signalArguments[0][0], [target])
            compare(dialogSpy.count, 2)
            compare(dialogSpy.signalArguments[1][1], "confirm")
            const confirm = dialogSpy.signalArguments[1][2]
            compare(confirm.title, "覆蓋 深夜.m3u8")
            compare(confirm.message, "這個檔案已經存在，覆蓋後無法復原。")
            compare(confirm.confirmText, "覆蓋")
            compare(confirm.destructive, true)
            coreDialogs.respond(dialogSpy.signalArguments[1][0], false)
            compare(plProbe.readText(target), "原本的內容")
        }

        function test_cancel_closes_without_save_dialog() {
            const id = plProbe.makePlaylist("取消", ["d"])
            PlaylistExport.open(id)
            const dialog = exportDialog()
            tryVerify(() => dialog.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.opened)
            compare(dialogSpy.count, 0)
        }
    }
}
