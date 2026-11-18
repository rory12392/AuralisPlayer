import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 專輯詳細頁（S-04，T-24）：開頁參數 {albumId}、頂部的專輯資訊、鍵盤走查（Tab 到「播放專輯」
//「加到佇列最後」、曲目清單的上下鍵與 Enter）、雙擊曲目、返回鈕、專輯不存在時的錯誤區塊。
// 檔名排在 tst_albums 之後：那邊先測「還沒登記時不做事」，這裡才換成真的頁面。
// Alt+← 是主視窗（AppShell）的 StandardKey.Back，這裡不建主視窗，只驗返回鈕與 Navigator.back。
Item {
    id: root
    width: 1100
    height: 720

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

    AlbumPage {
        id: page
        y: 40
        width: 1000
        height: 600
    }

    TestCase {
        name: "AlbumPage"
        when: windowShown

        function initTestCase() {
            pageProbe.addRealAlbumPage()
        }

        function init() {
            fakePlayback.reset(0, -1)
            verify(Navigator.open("library/album", { albumId: 1 }))
            tryVerify(() => page.ready && LibraryAlbum.albumId === 1, 5000)
            // 同一張專輯再開一次不重設清單（前一個測試可能選過第 2 首）
            find(page, "albumTrackList").currentIndex = 0
            start.forceActiveFocus()
        }

        function test_header() {
            compare(find(page, "albumTitle").text, "專輯 1")
            compare(find(page, "albumArtist").text, "藝人 1")
            compare(find(page, "albumSummary").text, "2001　2 首　6:00　96/24")
            compare(page.statusText, "2 首")
            compare(find(page, "albumCover").size, 200)
            // 曲目藝人都和專輯藝人相同：沒有「藝人」欄；一片：沒有組標題
            verify(!page.columns.some(c => c.id === "artist"))
            compare(LibraryAlbum.discCount, 1)
            compare(find(page, "albumTrackList").count, 2)
        }

        function test_keyboard() {
            keyClick(Qt.Key_Tab)
            const play = find(page, "albumPlay")
            verify(play.activeFocus)
            keyClick(Qt.Key_Return)
            compare(fakePlayback.calls, ["playFrom 0"])

            keyClick(Qt.Key_Tab)
            verify(find(page, "albumEnqueue").activeFocus)
            keyClick(Qt.Key_Return)
            compare(fakePlayback.calls, ["playFrom 0", "enqueueLast 2"])

            keyClick(Qt.Key_Tab)
            const list = find(page, "albumTrackList")
            verify(list.activeFocus)
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            keyClick(Qt.Key_Return)
            // AC-10-4：整張專輯換進佇列、從第 2 首播
            compare(fakePlayback.calls, ["playFrom 0", "enqueueLast 2", "playFrom 1"])
            compare(fakePlayback.paths, ["C:/t/1-1.flac", "C:/t/1-2.flac"])
        }

        function test_double_click_track() {
            const list = find(page, "albumTrackList")
            const row = list.itemAtIndex(1)
            verify(row !== null)
            mouseDoubleClickSequence(row, row.width / 2, row.height / 2)
            compare(fakePlayback.calls, ["playFrom 1"])
        }

        function test_back_button() {
            // 從專輯 1 開專輯 2：同一頁換參數，內容跟著換
            verify(Navigator.open("library/album", { albumId: 2 }))
            tryCompare(LibraryAlbum, "title", "專輯 2")
            const bar = page.toolbar.createObject(root)
            const back = find(bar, "albumBack")
            verify(back.enabled)
            compare(back.text, "返回")
            mouseClick(back)
            compare(Navigator.currentPage, "library/album")
            tryCompare(LibraryAlbum, "title", "專輯 1")
            bar.destroy()
        }

        // fix-m02-wheel：曲目清單放了 WheelScroll，一格滾輪捲「系統行數」× 曲目列高
        // （測試資料一張專輯只有 2 首捲不動，只核對元件與列高；捲動距離見 tst_wheel_scroll.qml）
        function test_wheel_scroll() {
            const list = find(page, "albumTrackList")
            let wheel = null
            for (let i = 0; i < list.children.length; ++i) {
                if (list.children[i].flickable === list && typeof list.children[i].scroll === "function")
                    wheel = list.children[i]
            }
            verify(wheel !== null, "曲目清單有 WheelScroll")
            compare(wheel.lineHeight, list.itemAtIndex(0).height)
        }

        function test_missing_album() {
            verify(Navigator.open("library/album", { albumId: 99 }))
            const error = find(page, "albumError")
            tryVerify(() => error.visible)
            verify(error.message.indexOf("找不到這張專輯") === 0)
            verify(!find(page, "albumHeader").visible)
            verify(!find(page, "albumTrackList").visible)
            compare(page.statusText, "")
            Navigator.back()
        }
    }
}
