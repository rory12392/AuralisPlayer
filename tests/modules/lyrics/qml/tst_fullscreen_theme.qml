import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lyr

// 全螢幕的深淺色（AC-17-6，S-15、S-16，T-117）：頁面宣告的主題跟著 LYR/fullscreenTheme、T 鍵與切換鈕
// 在深色與淺色之間切換並寫設定、跟隨主程式、淺色底色（封面主色提亮，沒有封面時 Paper 的 bg）、
// 切換時底色直接換、設定頁「歌詞與全螢幕」的三選一。主視窗殼（AppShell）把全螢幕頁的
// fullscreenTheme 繫到 ThemeController，這裡用同樣的兩個 Binding 代替殼。
Item {
    id: root
    width: 1600
    height: 900

    // 代替殼：全螢幕頁開著（設定頁的測試關掉，看主程式自己的主題）
    property bool fullscreenOpen: true

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

    Binding {
        target: ThemeController
        property: "fullscreen"
        value: root.fullscreenOpen
    }
    Binding {
        target: ThemeController
        property: "fullscreenTheme"
        value: page.fullscreenTheme
    }

    FullscreenPage {
        id: page
        anchors.fill: parent
        visible: root.fullscreenOpen
    }

    Rectangle {
        id: settingsFrame
        visible: !root.fullscreenOpen
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

    TestCase {
        name: "FullscreenTheme"
        when: windowShown

        function grab(item, name) {
            let saved = false
            item.grabToImage(result => { saved = result.saveToFile(shotDir + "/" + name + ".png") })
            tryVerify(() => saved, 3000)
        }

        function init() {
            root.fullscreenOpen = true
            ThemeController.mode = "void"
            Lyrics.setFullscreenTheme("dark")
            page.idleDelay = 3000
            page.wake()
            page.forceActiveFocus()
        }

        function cleanupTestCase() {
            Lyrics.setFullscreenTheme("dark")
            root.fullscreenOpen = false
            ThemeController.mode = "system"
        }

        function loadCover(rgbHex) {
            lyricsProbe.setNextCover(rgbHex)
            lyricsProbe.load("")
            tryVerify(() => Lyrics.fullscreenCoverColor === (rgbHex === "" ? "" : "#" + rgbHex), 5000)
        }

        function test_a_default_dark() {
            compare(page.fullscreenTheme, "dark")
            ThemeController.mode = "paper"
            verify(Theme.dark) // 主程式淺色時全螢幕照舊深色（v1.0 的行為）
            loadCover("ff0000")
            compare(String(page.backdrop), "#240f0f")
        }

        function test_b_t_key_switches_instantly() {
            loadCover("ff0000")
            const rect = root.find(page, "fsBackdrop")
            tryVerify(() => Qt.colorEqual(rect.color, "#240f0f"), 3000)
            keyClick(Qt.Key_T)
            // 同一個呼叫裡換好：設定、有效主題、底色都不過渡
            compare(Lyrics.fullscreenTheme, "light")
            compare(page.fullscreenTheme, "light")
            verify(!Theme.dark)
            const light = ThemeController.lightBackdrop("#ff0000")
            verify(Qt.colorEqual(page.backdrop, light))
            verify(Qt.colorEqual(rect.color, light), "底色沒有直接換：" + rect.color)
            // 文字用 Paper 的一般 token
            compare(root.find(page, "fsTitle").color, Theme.ink)
            keyClick(Qt.Key_T)
            compare(Lyrics.fullscreenTheme, "dark")
            verify(Theme.dark)
            verify(Qt.colorEqual(rect.color, "#240f0f"))
            // 帶組合鍵的 T 不接
            keyClick(Qt.Key_T, Qt.ControlModifier)
            compare(Lyrics.fullscreenTheme, "dark")
        }

        function test_c_button_toggles_and_keeps_focus_on_page() {
            const button = root.find(page, "fsThemeButton")
            verify(button)
            compare(button.iconName, "contrast")
            compare(button.text, "切換成淺色")
            compare(button.width, 24)
            mouseClick(button)
            compare(Lyrics.fullscreenTheme, "light")
            compare(button.text, "切換成深色")
            // 滑鼠按完焦點留在頁面：空白鍵照舊是播放／暫停，不是再按一次切換鈕
            verify(page.activeFocus)
            mouseClick(button)
            compare(Lyrics.fullscreenTheme, "dark")
        }

        function test_d_button_focus_keeps_bars() {
            const button = root.find(page, "fsThemeButton")
            button.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(Lyrics.fullscreenTheme, "light")
            page.idleDelay = 100
            page.wake()
            wait(300)
            verify(page.controlsShown)
            page.forceActiveFocus()
            tryVerify(() => !page.controlsShown, 2000)
        }

        function test_e_follow() {
            Lyrics.setFullscreenTheme("follow")
            compare(page.fullscreenTheme, "follow")
            ThemeController.mode = "paper"
            verify(!Theme.dark)
            ThemeController.mode = "void"
            verify(Theme.dark)
            // 跟隨時切換鈕依目前看到的主題換成另一個明確的值
            ThemeController.mode = "paper"
            const button = root.find(page, "fsThemeButton")
            compare(button.text, "切換成深色")
            mouseClick(button)
            compare(Lyrics.fullscreenTheme, "dark")
            verify(Theme.dark)
            // T 鍵同樣：主程式深色時跟隨＝看到深色，按 T 換成淺色
            Lyrics.setFullscreenTheme("follow")
            ThemeController.mode = "void"
            verify(Theme.dark)
            keyClick(Qt.Key_T)
            compare(Lyrics.fullscreenTheme, "light")
        }

        function test_f_light_backdrop_without_cover() {
            loadCover("")
            Lyrics.setFullscreenTheme("light")
            verify(Qt.colorEqual(page.backdrop, "#f4f2ee"))
            tryVerify(() => Qt.colorEqual(root.find(page, "fsBackdrop").color, "#f4f2ee"), 2000)
        }

        function test_g_light_backdrop_follows_cover() {
            Lyrics.setFullscreenTheme("light")
            loadCover("0000ff")
            const light = ThemeController.lightBackdrop("#0000ff")
            verify(Qt.colorEqual(page.backdrop, light))
            verify(!Qt.colorEqual(light, "#f4f2ee"))
            // 換歌照舊以 motionTint 過渡
            tryVerify(() => Qt.colorEqual(root.find(page, "fsBackdrop").color, light), 3000)
        }

        function test_h_key_hints() {
            const hints = root.find(page, "fsKeyHints")
            verify(hints.text.indexOf("T 深淺色") >= 0)
            verify(hints.text.indexOf("Esc 離開") >= 0)
        }

        function test_i_settings_section() {
            root.fullscreenOpen = false
            compare(settingsSection.title, "歌詞與全螢幕")
            const control = root.find(settingsSection, "lyricsFullscreenTheme")
            verify(control)
            compare(control.model.map(o => o.text), ["深色", "淺色", "跟隨主程式"])
            compare(control.currentIndex, 0)
            control.activated(1)
            compare(Lyrics.fullscreenTheme, "light")
            compare(control.currentIndex, 1)
            control.activated(2)
            compare(Lyrics.fullscreenTheme, "follow")
            // 從全螢幕頁改的也對回來
            Lyrics.setFullscreenTheme("dark")
            compare(control.currentIndex, 0)
            // 主程式自己的主題不受影響
            ThemeController.mode = "void"
            verify(Theme.dark)
            wait(50)
            grab(settingsFrame, "settings-lyrics-fullscreen-void")
            ThemeController.mode = "paper"
            verify(!Theme.dark)
            wait(Theme.motionState + 100)
            grab(settingsFrame, "settings-lyrics-fullscreen-paper")
        }
    }
}
