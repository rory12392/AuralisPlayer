import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lyr

// 全螢幕正在播放（S-15，T-68）：版面（大封面、曲名、藝人與專輯、進度、輸出狀態、上下兩列）、
// 歌詞的四種狀態、按鍵（空白鍵、左右鍵、進度條有焦點時的左右鍵、Esc 不接）、
// 滑鼠閒置隱藏游標與上下兩列、點歌詞行跳轉、底色屬性。
Item {
    id: root
    width: 1600
    height: 900

    // 殼收到頁面沒用掉的 Esc（核心的 AppShell 這時離開全螢幕）
    property int shellEscapes: 0

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
            text += "[00:" + (i + 1 < 10 ? "0" : "") + (i + 1) + ".00]全螢幕第 " + (i + 1) + " 行\n"
        return text
    }

    Item {
        id: shell
        anchors.fill: parent
        Keys.onEscapePressed: root.shellEscapes += 1

        FullscreenPage {
            id: page
            anchors.fill: parent
        }
    }

    TestCase {
        name: "FullscreenPage"
        when: windowShown

        function init() {
            fakePlayback.reset(3, 0)
            fakePlayback.setOutput("WASAPI 獨占", [], "")
            fakePlayback.tick(0)
            page.idleDelay = 3000
            page.wake()
            page.forceActiveFocus()
        }

        function loadSynced() {
            lyricsProbe.load(root.syncedText())
            tryVerify(() => Lyrics.state === "synced", 5000)
            tryVerify(() => page.showLyrics, 2000)
        }

        function test_ac17_1_layout() {
            // 封面＝視窗高的 54%（900 → 486），最大 720
            const cover = root.find(page, "fsCover")
            compare(cover.size, 486)
            root.height = 1600
            compare(cover.size, 720)
            root.height = 900
            compare(root.find(page, "fsTopBar").height, 64)
            compare(root.find(page, "fsBottomBar").height, 96)
            // 曲名（textTitle）、「<藝人>　<專輯>」
            const title = root.find(page, "fsTitle")
            tryCompare(title, "text", "全螢幕曲名 0", 5000)
            compare(title.font.pixelSize, Theme.textTitle)
            compare(title.color, Theme.fsInk)
            tryCompare(root.find(page, "fsArtistAlbum"), "text", "全螢幕藝人　全螢幕專輯", 5000)
            // 上列：左「正在播放」，右三組按鍵說明
            compare(root.find(page, "fsTopLabel").text, "正在播放")
            const hints = root.find(page, "fsKeyHints")
            verify(hints.text.indexOf("空白鍵 播放／暫停") >= 0)
            verify(hints.text.indexOf("← → 切歌") >= 0)
            verify(hints.text.indexOf("Esc 離開") >= 0)
            // 下列：進度、時間、輸出狀態（等寬「<格式>　<取樣率> / <位元深度>　<輸出路徑>」＋狀態標籤）
            tryCompare(root.find(page, "fsOutputText"), "text", "FLAC　96 kHz / 24 bit　WASAPI 獨占", 5000)
            compare(root.find(page, "fsStatusTag").text, "位元完美")
            fakePlayback.setOutput("WASAPI 共享", ["經 Windows 混音"], "")
            verify(root.find(page, "fsStatusTag").text !== "位元完美")
            compare(root.find(page, "fsProgress").to, 200000)
            fakePlayback.tick(65000)
            compare(root.find(page, "fsPosition").text, "1:05")
            compare(root.find(page, "fsDuration").text, "3:20")
        }

        function test_ac17_1_synced_lyrics() {
            loadSynced()
            const list = root.find(page, "fsLyrics")
            verify(list.visible)
            verify(list.synced)
            compare(list.activeFont.pixelSize, Theme.textDisplay)
            compare(list.lineFont.pixelSize, Theme.textLyric)
            compare(list.activeColor, Theme.fsInk)
            compare(list.lineColor, Theme.fsInkMuted)
            // 目前行垂直置中
            compare(list.anchorRatio, 0.5)
            fakePlayback.tick(5500)
            tryCompare(list, "activeLine", 4)
        }

        function test_lyrics_plain_static() {
            lyricsProbe.load("純文字第一行\n純文字第二行")
            tryVerify(() => Lyrics.state === "plain", 5000)
            const list = root.find(page, "fsLyrics")
            tryVerify(() => list.visible)
            verify(!list.synced)
        }

        function test_lyrics_none_centered() {
            loadSynced()
            const info = root.find(page, "fsInfo")
            verify(info.x + info.width / 2 < page.width / 2)
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "none", 5000)
            tryVerify(() => !page.showLyrics)
            verify(!root.find(page, "fsLyrics").visible)
            // 封面與曲名置中
            tryVerify(() => Math.abs(info.mapToItem(page, 0, 0).x + info.width / 2 - page.width / 2) < 1)
            verify(!root.find(page, "fsUnavailable").visible)
        }

        function test_lyrics_instrumental_and_unavailable() {
            lyricsProbe.setOnline(true)
            lyricsProbe.setOnlineResult("instrumental")
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "instrumental", 5000)
            verify(!page.showLyrics)
            lyricsProbe.setOnlineResult("unavailable")
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "unavailable", 5000)
            verify(!page.showLyrics)
            const note = root.find(page, "fsUnavailable")
            verify(note.visible)
            compare(note.text, "線上歌詞暫時無法取得")
            lyricsProbe.setOnlineResult("")
        }

        function test_lyrics_loading_keeps_layout() {
            loadSynced()
            lyricsProbe.load(root.syncedText(), 600)
            tryVerify(() => Lyrics.state === "loading", 2000)
            // 載入中不換版面，等結果出來再決定
            verify(page.showLyrics)
            tryVerify(() => Lyrics.state === "synced", 5000)
        }

        function test_ac15_3_click_line_seeks() {
            loadSynced()
            fakePlayback.tick(2500)
            const list = root.find(page, "fsLyrics")
            tryCompare(list, "activeLine", 1)
            wait(Theme.motionPanel + 100)
            const item = list.itemAtIndex(2)
            verify(item !== null)
            mouseClick(item, item.width / 2, item.height / 2)
            verify(fakePlayback.calls.indexOf("seek 3000") >= 0)
        }

        function test_ac17_3_keys() {
            page.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(fakePlayback.calls.indexOf("togglePause") >= 0 || fakePlayback.calls.indexOf("play") >= 0, true)
            keyClick(Qt.Key_Right)
            verify(fakePlayback.calls.indexOf("next") >= 0)
            keyClick(Qt.Key_Left)
            verify(fakePlayback.calls.indexOf("previous") >= 0)
            // Esc 不接：傳到殼（離開全螢幕）
            const before = root.shellEscapes
            keyClick(Qt.Key_Escape)
            compare(root.shellEscapes, before + 1)
        }

        function test_ac17_3_progress_focus_uses_arrows_to_seek() {
            const progress = root.find(page, "fsProgress")
            progress.forceActiveFocus()
            fakePlayback.tick(60000)
            const calls = fakePlayback.calls.length
            keyClick(Qt.Key_Right)
            verify(fakePlayback.calls.indexOf("seek 65000") >= 0)
            verify(fakePlayback.calls.indexOf("next") < 0)
            // 進度條有焦點時上下兩列不藏
            page.idleDelay = 100
            page.wake()
            wait(300)
            verify(page.controlsShown)
            page.forceActiveFocus()
        }

        function test_ac17_4_idle_hides_cursor_and_bars() {
            page.idleDelay = 200
            page.wake()
            verify(page.controlsShown)
            tryVerify(() => page.idle, 2000)
            verify(!page.controlsShown)
            const shield = root.find(page, "fsCursorShield")
            verify(shield.visible)
            compare(shield.cursorShape, Qt.BlankCursor)
            const top = root.find(page, "fsTopBar")
            tryCompare(top, "opacity", 0, 2000)
            // 鍵盤不讓它們出現
            keyClick(Qt.Key_Right)
            verify(page.idle)
            // 滑鼠一動就淡入（之後 3 秒內不再藏）
            page.idleDelay = 3000
            mouseMove(page, 400, 300)
            mouseMove(page, 420, 310)
            tryVerify(() => !page.idle, 1000)
            verify(!shield.visible)
            tryCompare(top, "opacity", 1, 2000)
            // 預設 3 秒：1.5 秒時還在
            page.wake()
            wait(1500)
            verify(!page.idle)
        }

        function test_scrollbar_hidden_until_hover_and_with_bars() {
            loadSynced()
            const list = root.find(page, "fsLyrics")
            const bar = root.find(list, "lyricsScrollBar")
            verify(bar)
            mouseMove(page, 40, 450) // 封面那一側，不在歌詞上
            tryCompare(bar, "opacity", 0, 2000)
            mouseMove(list, 100, 300)
            tryCompare(bar, "opacity", 1, 2000)
            // 控制列隱藏時捲軸也不顯示（滑鼠還停在歌詞上）
            page.idleDelay = 200
            page.wake()
            tryVerify(() => page.idle, 2000)
            tryCompare(bar, "opacity", 0, 2000)
        }

        function test_backdrop_property() {
            // 底色是一個屬性，繫結 Lyrics.fullscreenBackdrop；沒有封面時 #050505
            lyricsProbe.setNextCover("")
            lyricsProbe.load("")
            tryCompare(Lyrics, "fullscreenBackdrop", "#050505", 5000)
            compare(String(page.backdrop), "#050505")
            tryVerify(() => Qt.colorEqual(root.find(page, "fsBackdrop").color, page.backdrop), 2000)
        }

        // 兩個顏色每個分量差多少（0–1）；可以給「#rrggbb」字串
        function colorDistance(a, b) {
            if (typeof a === "string")
                a = Qt.color(a)
            if (typeof b === "string")
                b = Qt.color(b)
            return Math.max(Math.abs(a.r - b.r), Math.abs(a.g - b.g), Math.abs(a.b - b.b))
        }

        function test_ac17_2_backdrop_follows_cover_with_tint() {
            const rect = root.find(page, "fsBackdrop")
            lyricsProbe.setNextCover("")
            lyricsProbe.load("")
            tryVerify(() => Qt.colorEqual(rect.color, "#050505"), 5000)

            // 換到紅色封面：底色是壓暗後的值，畫面以 motionTint（600 ms）過渡，不是立刻跳過去
            compare(Theme.motionTint, 600)
            lyricsProbe.setNextCover("ff0000")
            lyricsProbe.load("")
            tryCompare(Lyrics, "fullscreenBackdrop", "#240f0f", 5000)
            verify(!Qt.colorEqual(rect.color, "#240f0f"))
            // 一路取樣到抵達：中間看得到過渡色，而且花了大約 motionTint 的時間
            const started = Date.now()
            let between = 0
            while (!Qt.colorEqual(rect.color, "#240f0f") && Date.now() - started < 3000) {
                if (colorDistance(rect.color, "#050505") > 0.01 && colorDistance(rect.color, "#240f0f") > 0.01)
                    ++between
                wait(16)
            }
            const elapsed = Date.now() - started
            verify(Qt.colorEqual(rect.color, "#240f0f"), "3 秒內沒有到達目標色")
            verify(elapsed >= 400, "過渡太快：" + elapsed + " ms")
            verify(between >= 3, "看不到過渡色：" + between)

            // 過渡到一半又換歌：從目前顯示的顏色接續，不跳色
            lyricsProbe.setNextCover("0000ff")
            lyricsProbe.load("")
            tryCompare(Lyrics, "fullscreenBackdrop", "#0f0f24", 5000)
            wait(250)
            const midway = Qt.rgba(rect.color.r, rect.color.g, rect.color.b, 1)
            lyricsProbe.setNextCover("00ff00")
            lyricsProbe.load("")
            tryCompare(Lyrics, "fullscreenBackdrop", "#0f240f", 5000)
            verify(colorDistance(rect.color, midway) < 0.03)
            tryVerify(() => Qt.colorEqual(rect.color, "#0f240f"), 2000)

            // 沒有封面：回到 #050505
            lyricsProbe.setNextCover("")
            lyricsProbe.load("")
            tryVerify(() => Qt.colorEqual(rect.color, "#050505"), 5000)
        }

        function test_zz_screenshots() {
            loadSynced()
            fakePlayback.tick(6500)
            wait(Theme.motionPanel + 200)
            let done = false
            page.grabToImage(result => { result.saveToFile(shotDir + "/fullscreen-synced-1600.png"); done = true })
            tryVerify(() => done, 5000)
            lyricsProbe.load("")
            tryVerify(() => !page.showLyrics, 5000)
            wait(200)
            done = false
            page.grabToImage(result => { result.saveToFile(shotDir + "/fullscreen-none-1600.png"); done = true })
            tryVerify(() => done, 5000)
        }
    }
}
