import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lyr

// LRCLIB 帶來的面板狀態與設定頁的「歌詞」分區（T-67）：純音樂、線上歌詞暫時無法取得＋「重試」
// （AC-16-2）、來源「LRCLIB」、設定頁兩個 Checkbox 即時生效（AC-16-3、AC-16-4）。線上那段是
// 測試替身（lyricsProbe.setOnlineResult），不連線。截圖用 1600×900 視窗的面板高度，給人看、不比對。
Item {
    id: root
    width: 1600
    height: 900

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

    Rectangle {
        id: settingsFrame
        y: 40
        width: 720 + 2 * Theme.space6
        height: settingsSection.implicitHeight + 2 * Theme.space6
        color: Theme.bg

        LyricsSettingsSection {
            id: settingsSection
            x: Theme.space6
            y: Theme.space6
            width: 720
        }
    }

    // 面板在 1600×900 主視窗右側的高度（扣掉工具列、狀態列與播放列）
    Rectangle {
        id: panelFrame
        x: root.width - Theme.panelWidth
        y: 40
        width: Theme.panelWidth
        height: 640
        color: Theme.surface

        LyricsPanel {
            id: panel
            anchors.fill: parent
        }
    }

    LyricsPreview {
        id: preview
        x: 800
        y: 40
        width: 300
    }

    TestCase {
        name: "LyricsOnline"
        when: windowShown

        function grab(item, name) {
            let saved = false
            item.grabToImage(result => { saved = result.saveToFile(shotDir + "/" + name + ".png") })
            tryVerify(() => saved, 3000)
        }

        function init() {
            start.forceActiveFocus()
            lyricsProbe.setOnline(true)
            lyricsProbe.setOnlineResult("")
        }

        function cleanupTestCase() {
            lyricsProbe.setOnline(true)
            lyricsProbe.setOnlineResult("")
        }

        function test_a_instrumental() {
            lyricsProbe.setOnlineResult("instrumental")
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "instrumental", 5000)
            const empty = root.find(panel, "lyricsInstrumental")
            verify(empty.visible)
            compare(empty.title, "純音樂")
            verify(!root.find(panel, "lyricsEmpty").visible)
            verify(!root.find(panel, "lyricsList").visible)
            compare(root.find(preview, "lyricsPreviewCurrent").text, "純音樂")
            wait(50)
            grab(panelFrame, "lyrics-instrumental")
        }

        function test_b_unavailable_then_retry() {
            lyricsProbe.setOnlineResult("unavailable")
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "unavailable", 5000)
            const block = root.find(panel, "lyricsUnavailable")
            verify(block.visible)
            compare(block.title, "線上歌詞暫時無法取得")
            compare(block.actionText, "重試")
            compare(root.find(preview, "lyricsPreviewCurrent").text, "線上歌詞暫時無法取得")
            wait(50)
            grab(panelFrame, "lyrics-unavailable")

            // 「重試」：這次查到同步歌詞，來源寫 LRCLIB
            lyricsProbe.setOnlineResult("synced")
            const retry = findButton(block)
            verify(retry !== null)
            mouseClick(retry)
            tryVerify(() => Lyrics.state === "synced", 5000)
            compare(Lyrics.source, "lrclib")
            compare(root.find(panel, "lyricsSource").text, "來源：LRCLIB")
        }

        // EmptyState 的按鈕是共用 Button：找文字是「重試」的那一個
        function findButton(item) {
            if (!item)
                return null
            if (item.text === "重試" && item.width > 0)
                return item
            const kids = item.children
            for (let i = 0; i < kids.length; ++i) {
                const found = findButton(kids[i])
                if (found)
                    return found
            }
            return null
        }

        function test_c_settings_section() {
            const online = root.find(settingsSection, "lyricsOnline")
            const saveLrc = root.find(settingsSection, "lyricsSaveLrc")
            compare(online.text, "從 LRCLIB 抓取歌詞")
            compare(online.description, "本機找不到歌詞時才會連線，只送出曲名、藝人、專輯與長度。")
            compare(saveLrc.text, "把抓到的歌詞另存為同名 .lrc")
            compare(saveLrc.description, "只新增 .lrc 檔，不修改音樂檔。")
            verify(online.checked)       // 未設定＝開
            verify(!saveLrc.checked)     // 預設關
            wait(50)
            grab(settingsFrame, "lyrics-settings")

            // 關掉線上抓取：本機沒有歌詞的新曲目不查線上，面板帶說明
            lyricsProbe.setOnlineResult("synced")
            mouseClick(online)
            tryVerify(() => !Lyrics.onlineEnabled, 2000)
            verify(!online.checked)
            lyricsProbe.load("")
            tryVerify(() => Lyrics.state === "none", 5000)
            compare(root.find(panel, "lyricsEmpty").description, "線上抓取已關閉，可在設定開啟。")
            // 打開：目前這首立刻重找（關閉時記的 notfound 不算數）
            mouseClick(online)
            tryVerify(() => Lyrics.onlineEnabled && Lyrics.state === "synced", 5000)

            mouseClick(saveLrc)
            verify(Lyrics.saveLrc)
            verify(saveLrc.checked)
            mouseClick(saveLrc)
            verify(!Lyrics.saveLrc)
        }
    }
}
