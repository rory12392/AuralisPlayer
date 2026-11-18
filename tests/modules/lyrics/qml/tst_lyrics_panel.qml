import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lyr

// 歌詞面板（S-12，T-66）與正在播放面板的歌詞預覽（S-11）：載入中、沒有歌詞（含線上抓取關閉的說明）、
// 純文字、同步歌詞的目前行樣式與 40% 置中、手動捲動後暫停再恢復、點行與鍵盤（Tab、上下鍵、Enter）跳轉、
// 預覽三行與點一下切到歌詞頁籤。
Item {
    id: root
    width: 800
    height: 700

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

    // 20 行同步歌詞，第 n 行（從 0 起）在 n+1 秒
    function syncedText() {
        let text = ""
        for (let i = 0; i < 20; ++i)
            text += "[00:" + (i + 1 < 10 ? "0" : "") + (i + 1) + ".00]第 " + (i + 1) + " 行\n"
        return text
    }

    Button { id: start; text: "起點" }

    // 面板在主視窗裡的底色（截圖看得出深色主題）
    Rectangle {
        id: panelFrame
        y: 40
        width: Theme.panelWidth
        height: 600
        color: Theme.surface

        LyricsPanel {
            id: panel
            anchors.fill: parent
        }
    }

    LyricsPreview {
        id: preview
        x: 400
        y: 40
        width: 300
    }

    SignalSpy {
        id: panelSpy
        target: Navigator
        signalName: "panelRequested"
    }

    TestCase {
        name: "LyricsPanel"
        when: windowShown

        property Item list: root.find(panel, "lyricsList")

        function loadSynced() {
            fakePlayback.tick(0)
            lyricsProbe.load(root.syncedText())
            tryVerify(() => Lyrics.state === "synced", 5000)
            list.resumeDelay = 300
            wait(50)
        }

        // 目前行的中心在清單高度的幾成處
        function activeRatio() {
            const item = list.itemAtIndex(Lyrics.currentLine)
            const point = item.mapToItem(list, 0, item.height / 2)
            return point.y / list.height
        }

        function init() {
            start.forceActiveFocus()
            panelSpy.clear()
            lyricsProbe.setOnline(true)
            list.scrollBarAllowed = true
        }

        function test_a_loading_then_empty() {
            lyricsProbe.load("", 400)
            compare(Lyrics.state, "loading")
            verify(root.find(panel, "lyricsLoading").visible)
            verify(!list.visible)
            tryVerify(() => Lyrics.state === "none", 5000)
            const empty = root.find(panel, "lyricsEmpty")
            verify(empty.visible)
            compare(empty.title, "沒有歌詞")
            compare(empty.description, "")
            verify(!list.visible)
            verify(!root.find(panel, "lyricsLoading").visible)
            // 預覽只佔一行說明
            compare(root.find(preview, "lyricsPreviewCurrent").text, "沒有歌詞")
            verify(!root.find(preview, "lyricsPreviewPrevious").visible)
        }

        function test_b_empty_online_off() {
            lyricsProbe.setOnline(false)
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "none" && !Lyrics.onlineEnabled, 5000)
            compare(root.find(panel, "lyricsEmpty").description, "線上抓取已關閉，可在設定開啟。")
        }

        function test_c_plain() {
            lyricsProbe.load("第一段\n第二段")
            tryVerify(() => Lyrics.state === "plain", 5000)
            verify(root.find(panel, "lyricsPlainNote").visible)
            compare(root.find(panel, "lyricsPlainNote").text, "這份歌詞沒有時間軸")
            verify(list.visible)
            compare(list.count, 2)
            compare(root.find(panel, "lyricsSource").text, "來源：檔案內嵌")
            // 不高亮：每一行都是一般樣式；點了不跳
            wait(50)
            const first = list.itemAtIndex(0)
            compare(first.active, false)
            const calls = fakePlayback.calls.length
            mouseClick(first)
            compare(fakePlayback.calls.length, calls)
            compare(root.find(preview, "lyricsPreviewCurrent").text, "這份歌詞沒有時間軸")
        }

        function test_d_synced_style_and_center() {
            loadSynced()
            verify(!root.find(panel, "lyricsPlainNote").visible)
            fakePlayback.tick(10500) // 第 10 行（索引 9）
            compare(Lyrics.currentLine, 9)
            const item = list.itemAtIndex(9)
            verify(item.active)
            compare(item.children[0].font.pixelSize, Theme.textHeading)
            compare(item.children[0].color, Theme.ink)
            compare(list.itemAtIndex(8).children[0].font.pixelSize, Theme.textBody)
            compare(list.itemAtIndex(8).children[0].color, Theme.inkMuted)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            // 拖曳進度列（跳轉）後立刻跳到對應行，並捲過去
            fakePlayback.seek(15200)
            compare(Lyrics.currentLine, 14)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            // 預覽三行
            compare(root.find(preview, "lyricsPreviewPrevious").text, "第 14 行")
            compare(root.find(preview, "lyricsPreviewCurrent").text, "第 15 行")
            compare(root.find(preview, "lyricsPreviewNext").text, "第 16 行")
            compare(root.find(preview, "lyricsPreviewCurrent").color, Theme.ink)
            compare(root.find(preview, "lyricsPreviewNext").color, Theme.inkMuted)
            compare(root.find(panel, "lyricsSource").text, "來源：檔案內嵌")
        }

        function test_e_manual_scroll_pauses() {
            loadSynced()
            fakePlayback.tick(5500)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            verify(list.following)
            mouseWheel(list, 100, 200, 0, -120) // 使用者用滾輪往下捲
            verify(!list.following)
            wait(50)
            const y = list.contentY
            fakePlayback.tick(6500) // 換行時不自動置中
            compare(Lyrics.currentLine, 5)
            wait(100)
            // 只容許行高改變（目前行換字級）造成的零點幾像素；自動置中會移動將近一整行
            verify(Math.abs(list.contentY - y) < 4, "contentY " + list.contentY + " vs " + y)
            // 停手一段時間（測試把 3 秒縮成 0.3 秒）後恢復置中
            tryVerify(() => list.following, 2000)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
        }

        function test_e2_scrollbar_only_when_scrolling_or_hovered() {
            loadSynced()
            const bar = root.find(list, "lyricsScrollBar")
            verify(bar)
            verify(bar.size < 1) // 歌詞比清單長，捲軸有作用
            mouseMove(root, root.width - 5, root.height - 5) // 滑鼠不在清單上
            tryVerify(() => list.following, 2000)
            tryCompare(bar, "opacity", 0, 2000) // 沒有常駐的線
            mouseMove(list, 100, 200)
            tryCompare(bar, "opacity", 1, 2000) // 滑鼠移到清單上
            mouseMove(root, root.width - 5, root.height - 5)
            tryCompare(bar, "opacity", 0, 2000)
        }

        // 捲軸隱藏（全螢幕控制列隱藏時的 scrollBarAllowed false）時不攔清單右緣的輸入（AC-15-3）：
        // 點右緣的歌詞行照樣跳轉；從右緣往下拖是捲清單（contentY 不增加），不是拖捲軸
        function test_e3_hidden_scrollbar_passes_input() {
            loadSynced()
            fakePlayback.tick(10500)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            list.scrollBarAllowed = false
            const bar = root.find(list, "lyricsScrollBar")
            tryCompare(bar, "opacity", 0, 2000)
            verify(bar.width > 2)
            const edge = list.width - 2
            const y = list.height / 2
            const before = list.contentY
            mouseDrag(list, edge, y, 0, 120)
            wait(100)
            verify(list.contentY <= before + 1, "contentY " + list.contentY + " vs " + before)
            tryVerify(() => list.following, 2000)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            const item = list.itemAtIndex(12)
            const point = item.mapToItem(list, 0, item.height / 2)
            mouseClick(list, edge, point.y)
            compare(fakePlayback.calls[fakePlayback.calls.length - 1], "seek 13000")
            list.scrollBarAllowed = true
        }

        function test_f_click_seeks() {
            loadSynced()
            fakePlayback.tick(2500)
            const item = list.itemAtIndex(3)
            mouseClick(item)
            compare(fakePlayback.calls[fakePlayback.calls.length - 1], "seek 4000")
            compare(Lyrics.currentLine, 3)
        }

        function test_g_keyboard() {
            loadSynced()
            fakePlayback.tick(3500) // 第 3 行（索引 2）
            keyClick(Qt.Key_Tab) // 從起點按鈕 Tab 到歌詞清單
            verify(list.activeFocus)
            compare(list.currentIndex, 2)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 4)
            keyClick(Qt.Key_Return)
            compare(fakePlayback.calls[fakePlayback.calls.length - 1], "seek 5000")
            compare(Lyrics.currentLine, 4)
        }

        function grab(item, name) {
            let saved = false
            item.grabToImage(result => { saved = result.saveToFile(shotDir + "/" + name + ".png") })
            tryVerify(() => saved, 3000)
        }

        // 面板在 Void（深色）主題的樣子：同步歌詞（目前行在 40% 處）與沒有歌詞，給人看、不比對
        function test_i_grab_panels() {
            loadSynced()
            fakePlayback.tick(8500)
            tryVerify(() => Math.abs(activeRatio() - 0.4) < 0.02, 2000)
            grab(panelFrame, "lyrics-synced")
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "none", 5000)
            wait(50)
            grab(panelFrame, "lyrics-empty")
        }

        function test_h_preview_opens_panel() {
            loadSynced()
            mouseClick(preview)
            compare(panelSpy.count, 1)
            compare(panelSpy.signalArguments[0][0], "panel/lyrics")
        }
    }
}
