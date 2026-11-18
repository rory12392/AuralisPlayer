import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Pl

// 匯入播放清單（T-51，US-12；畫面規格 S-09）：側欄「匯入播放清單」開系統的開檔對話框
// （篩選四種副檔名、可多選）；有找不到的開結果對話框（讀數三格、找不到的清單、「略過」）；
// 讀不到時一段文字＋「關閉」；全部找到時不開對話框。
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

    SignalSpy { id: dialogSpy; target: coreDialogs; signalName: "requested" }

    TestCase {
        name: "PlaylistImport"
        when: windowShown

        function init() {
            plProbe.resetPlaylists()
            dialogSpy.clear()
            start.forceActiveFocus()
        }

        function importDialog() {
            return findChild(section, "playlistImportDialog")
        }

        function test_sidebar_item_opens_file_dialog() {
            const item = findChild(section, "importPlaylistItem")
            verify(item)
            compare(item.text, "匯入播放清單")
            mouseClick(item)
            compare(dialogSpy.count, 1)
            compare(dialogSpy.signalArguments[0][1], "files")
            const options = dialogSpy.signalArguments[0][2]
            compare(options.title, "匯入播放清單")
            compare(options.nameFilters, ["播放清單 (*.m3u *.m3u8 *.pls *.fpl)"])
            // 全部找到（沒有曲目也算）：不開結果對話框，建立播放清單
            const path = plProbe.writePlaylist("空的.m3u8", "#EXTM3U\n")
            coreDialogs.respond(dialogSpy.signalArguments[0][0], [path])
            compare(Playlists.playlists.length, 1)
            compare(Playlists.playlists[0].name, "空的")
            verify(!importDialog().opened)
        }

        function test_missing_shows_result_and_skip_closes() {
            const path = plProbe.writePlaylist("找不到.m3u8",
                                               "#EXTM3U\nD:\\不存在\\a.flac\n\\\\nas\\share\\b.flac\n")
            PlaylistImport.importFiles([path])
            const dialog = importDialog()
            tryVerify(() => dialog.opened)
            compare(dialog.title, "匯入「找不到.m3u8」")
            compare(findChild(dialog.contentItem, "importTotal").value, "2")
            compare(findChild(dialog.contentItem, "importFound").value, "0")
            compare(findChild(dialog.contentItem, "importMissing").value, "2")
            compare(findChild(dialog.contentItem, "importMissingList").count, 2)
            compare(dialog.cancelText, "略過")
            // S-09 的截圖（畫面審查用；對話框本身的那一塊）
            waitForRendering(dialog.contentItem)
            const shot = grabImage(dialog.background.parent)
            verify(shot.width > 0)
            shot.save(plProbe.tempPath("s09-import-result.png"))
            // 找不到的項目保留在清單裡
            compare(Playlists.playlists.length, 1)
            compare(Playlists.playlists[0].count, 2)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.opened)
            compare(Object.keys(PlaylistImport.result).length, 0)
        }

        // T-52（AC-12-3）：重新對應區預填共同資料夾；「瀏覽」開資料夾選擇；套用並重新檢查後數字更新，
        // 全部找到時按鈕變「完成」、重新對應區收起來
        function test_remap_area_prefill_browse_apply_done() {
            const dir = plProbe.quickDir()
            plProbe.makeFile("新家/專輯/a.flac")
            plProbe.makeFile("新家/b.flac")
            const path = plProbe.writePlaylist("搬家.m3u8", "#EXTM3U\n" + dir + "\\舊家\\專輯\\a.flac\n"
                                               + dir + "\\舊家\\b.flac\n")
            PlaylistImport.importFiles([path])
            const dialog = importDialog()
            tryVerify(() => dialog.opened)
            const remap = findChild(dialog.contentItem, "importRemap")
            verify(remap.visible)
            const oldRoot = findChild(remap, "remapOldRoot")
            const newRoot = findChild(remap, "remapNewRoot")
            const apply = findChild(remap, "remapApply")
            compare(oldRoot.text, dir + "\\舊家")
            compare(newRoot.text, "")
            compare(apply.text, "套用並重新檢查")
            verify(!apply.enabled)
            mouseClick(findChild(remap, "remapBrowse"))
            compare(dialogSpy.count, 1)
            compare(dialogSpy.signalArguments[0][1], "folder")
            coreDialogs.respond(dialogSpy.signalArguments[0][0], [dir + "\\新家"])
            compare(newRoot.text, dir + "\\新家")
            verify(apply.enabled)
            // S-09 重新對應區的截圖（畫面審查用）；按鈕剛變可用，等下一次繪製
            wait(200)
            waitForRendering(dialog.contentItem)
            const shot = grabImage(dialog.background.parent)
            verify(shot.width > 0)
            shot.save(plProbe.shotPath("s09-remap.png"))
            mouseClick(apply)
            tryVerify(() => findChild(dialog.contentItem, "importFound").value === "2")
            compare(findChild(dialog.contentItem, "importMissing").value, "0")
            compare(dialog.cancelText, "完成")
            verify(!remap.visible)
            verify(dialog.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.opened)
        }

        // F-1：掃描完成後的重新檢查改了項目數（整張 CUE 展開）時，側欄的曲目數跟著更新
        function test_recheck_updates_sidebar_count() {
            const dir = plProbe.quickDir()
            const path = plProbe.writePlaylist("展開.m3u8", "#EXTM3U\n" + dir + "\\之後才有\\a.flac\n")
            PlaylistImport.importFiles([path])
            const dialog = importDialog()
            tryVerify(() => dialog.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.opened)
            const id = Playlists.playlists[0].id
            compare(Playlists.playlists[0].count, 1)
            plProbe.makeFile("之後才有/a.flac")
            plProbe.addTrackBehind(id, dir + "\\之後才有\\b.flac")
            plProbe.recheckAll()
            tryVerify(() => Playlists.info(id).count === 2)
        }

        function test_unreadable_shows_message() {
            PlaylistImport.importFiles(["C:\\不存在的資料夾\\清單.m3u"])
            const dialog = importDialog()
            tryVerify(() => dialog.opened)
            compare(findChild(dialog.contentItem, "importMessage").text,
                    "無法讀取 清單.m3u，請確認檔案存在而且沒有被其他程式鎖住。")
            compare(dialog.cancelText, "關閉")
            compare(Playlists.playlists.length, 0)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.opened)
        }
    }
}
