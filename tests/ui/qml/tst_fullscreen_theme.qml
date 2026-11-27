import QtQuick
import QtTest
import Auralis.Ui

// 全螢幕一律用 Void 的元件顏色（T-94，DESIGN §2.3、畫面規格 S-15）：Paper 主題下進全螢幕頁，
// 頁內的共用元件（Slider、StatusTag、ScrollBar）用 Void 的 token；離開後回到 Paper，
// 使用者的主題設定（ThemeController.mode）不變；全螢幕期間改的主題設定在離開後套用。
// 進出全螢幕的同一個呼叫裡主題就換好（中間不會畫出一幀主題不對的畫面）。
Item {
    id: root
    width: 1920
    height: 1080

    AppShell {
        id: shell
        width: root.width
        height: root.height
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    TestCase {
        name: "FullscreenTheme"
        when: windowShown

        // Void 與 Paper 的值（DESIGN §2.2）
        readonly property color voidBg: "#050505"
        readonly property color voidLineStrong: "#666666"
        readonly property color voidAmber: "#e0a34a"
        readonly property color paperBg: "#f4f2ee"
        readonly property color paperLineStrong: "#7a756c"
        readonly property color paperAmber: "#8f5810"

        property string originalMode

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function sameColor(actual, expected, what) {
            verify(Qt.colorEqual(actual, expected), what + "：" + actual + "，應為 " + expected)
        }

        function initTestCase() {
            originalMode = ThemeController.mode
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenPageFixture.qml", 950)
        }

        function cleanupTestCase() {
            ThemeController.overrideMode(originalMode)
        }

        function init() {
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenPageFixture.qml", 950)
            ThemeController.overrideMode("paper")
            verify(Navigator.open("settings"))
            shell.forceActiveFocus()
            verify(!Theme.dark)
        }

        function cleanup() {
            Navigator.leaveFullscreen()
        }

        function test_a_paper_fullscreen_uses_void() {
            verify(Navigator.enterFullscreen())
            // 同一個呼叫裡就換成 Void
            verify(Theme.dark)
            sameColor(Theme.bg, voidBg, "Theme.bg")
            compare(ThemeController.mode, "paper")

            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            const page = item("fixtureFullscreenPage")
            sameColor(page.color, voidBg, "全螢幕頁底色")
            const slider = item("fixtureFullscreenSlider")
            sameColor(slider.background.children[0].color, voidLineStrong, "Slider 軌道")
            sameColor(slider.background.children[1].color, voidAmber, "Slider 已播段")
            sameColor(item("fixtureFullscreenTag").dotColor, voidAmber, "StatusTag 指示點")
            sameColor(item("fixtureFullscreenScrollBar").contentItem.children[0].color, voidLineStrong,
                      "ScrollBar 把手")
            waitForRendering(shell)
            grabImage(shell).save(testTmpDir + "/t-94-fullscreen-paper.png")

            // 離開：同一個呼叫裡回到 Paper，主視窗不留下 Void 的顏色
            verify(Navigator.leaveFullscreen())
            verify(!Theme.dark)
            sameColor(Theme.bg, paperBg, "Theme.bg")
            sameColor(Theme.lineStrong, paperLineStrong, "Theme.lineStrong")
            sameColor(Theme.amber, paperAmber, "Theme.amber")
            compare(ThemeController.mode, "paper")
            compare(Navigator.currentPage, "settings")
        }

        function test_b_keys_switch_theme_too() {
            keyClick(Qt.Key_F11)
            verify(Navigator.fullscreen)
            verify(Theme.dark)
            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            tryVerify(() => item("fixtureFullscreenPage").activeFocus)
            keyClick(Qt.Key_Escape)
            verify(!Navigator.fullscreen)
            verify(!Theme.dark)
        }

        function test_c_setting_changed_during_fullscreen_applies_after() {
            verify(Navigator.enterFullscreen())
            verify(Theme.dark)
            // 全螢幕期間從別處把主題改成 Void，再改回 Paper：全螢幕裡一直是 Void
            ThemeController.overrideMode("void")
            verify(Theme.dark)
            ThemeController.overrideMode("paper")
            verify(Theme.dark)
            compare(ThemeController.mode, "paper")
            // 改成 Void 後離開：套用新設定
            ThemeController.overrideMode("void")
            verify(Navigator.leaveFullscreen())
            verify(Theme.dark)
            compare(ThemeController.mode, "void")
        }

        // T-121（AC-17-6、DESIGN §10 第 9 項）：強制淺色時一樣在同一個呼叫裡換好
        function test_d_forced_light_switches_in_same_call() {
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenLightFixture.qml", 950)
            ThemeController.overrideMode("void")
            verify(Theme.dark)
            verify(Navigator.enterFullscreen())
            verify(!Theme.dark)
            sameColor(Theme.bg, paperBg, "Theme.bg")
            compare(ThemeController.mode, "void")

            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            sameColor(item("fixtureFullscreenPage").color, paperBg, "全螢幕頁底色")
            const slider = item("fixtureFullscreenSlider")
            sameColor(slider.background.children[0].color, paperLineStrong, "Slider 軌道")
            sameColor(slider.background.children[1].color, paperAmber, "Slider 已播段")
            sameColor(item("fixtureFullscreenTag").dotColor, paperAmber, "StatusTag 指示點")
            sameColor(Theme.fsInk, Theme.ink, "fsInk 跟著有效主題")
            sameColor(Theme.fsAmber, paperAmber, "fsAmber 跟著有效主題")
            waitForRendering(shell)
            grabImage(shell).save(testTmpDir + "/t-121-fullscreen-light.png")

            verify(Navigator.leaveFullscreen())
            verify(Theme.dark)
            sameColor(Theme.bg, voidBg, "Theme.bg")
            compare(ThemeController.mode, "void")
        }

        function test_e_follow_uses_main_theme() {
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenFollowFixture.qml", 950)
            verify(Navigator.enterFullscreen())
            verify(!Theme.dark) // 主程式是 Paper
            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            sameColor(item("fixtureFullscreenPage").color, paperBg, "全螢幕頁底色")
            // 全螢幕期間主程式換成 Void：跟著變
            ThemeController.overrideMode("void")
            verify(Theme.dark)
            sameColor(item("fixtureFullscreenPage").color, voidBg, "全螢幕頁底色")
            verify(Navigator.leaveFullscreen())
            verify(Theme.dark)
        }

        // 全螢幕中切換深淺色：有 Behavior on color 的共用元件不淡入，同一格就是新顏色，
        // 不會有一格深字配深底（實心按鈕的底是 ink、字是 bg；勾選的圖示按鈕底是 surfaceRaised、圖示是 ink）
        function test_f_switch_inside_fullscreen_is_instant() {
            verify(Navigator.enterFullscreen())
            verify(Theme.dark)
            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            const page = item("fixtureFullscreenPage")
            const solid = item("fixtureFullscreenSolid")
            const toggle = item("fixtureFullscreenToggle")
            waitForRendering(shell)

            page.fullscreenTheme = "light"
            verify(!Theme.dark)
            sameColor(solid.background.color, Theme.ink, "實心按鈕底（淺色）")
            sameColor(toggle.background.color, Theme.surfaceRaised, "勾選的圖示按鈕底（淺色）")
            waitForRendering(shell)
            let image = grabImage(shell)
            image.save(testTmpDir + "/t-121-switch-to-light.png")
            sameColor(image.pixel(toggle.mapToItem(shell, 2, 2).x, toggle.mapToItem(shell, 2, 2).y),
                      Theme.surfaceRaised, "切換後第一格：圖示按鈕底")

            page.fullscreenTheme = "dark"
            verify(Theme.dark)
            sameColor(solid.background.color, Theme.ink, "實心按鈕底（深色）")
            sameColor(toggle.background.color, Theme.surfaceRaised, "勾選的圖示按鈕底（深色）")
            waitForRendering(shell)
            image = grabImage(shell)
            image.save(testTmpDir + "/t-121-switch-to-dark.png")
            sameColor(image.pixel(toggle.mapToItem(shell, 2, 2).x, toggle.mapToItem(shell, 2, 2).y),
                      Theme.surfaceRaised, "切換後第一格：圖示按鈕底")
            verify(!ThemeController.instantThemeSwitch)
        }
    }
}
