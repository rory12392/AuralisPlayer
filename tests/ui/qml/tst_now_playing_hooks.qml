import QtQuick
import QtTest
import Auralis.Ui

// 正在播放面板與全螢幕的核心接點（T-76，LYR 規格 §4 的 M-01 交接）：
// 1. 面板的模組區塊（addNowPlayingSection）在讀數下方的髮絲線之後；沒有登記時不佔位，載不到的不出現；
// 2. Navigator.showPanel 與 Ctrl+L 切到歌詞頁籤（收合時先展開；沒有歌詞面板時不做事）；
// 3. 點封面、F11 進全螢幕，F11、Esc 離開回到原頁；沒有全螢幕頁時封面不能點、F11 不做事；
//    全螢幕頁自己先拿到空白鍵、左右鍵與 Esc。
// 登記用 nowPlayingProbe.addFixture（這個測試檔的服務一開始只有佇列面板）。最後截有區塊與沒有區塊兩張。
Item {
    id: root
    width: 1600
    height: 900

    AppShell {
        id: shell
        width: root.width
        height: root.height
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    TestCase {
        name: "NowPlayingHooks"
        when: windowShown

        // 沒有任何模組登記時的內容高度（test_a 量，test_b 比較）
        property real bareHeight: 0

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function panelIndex(id) {
            return UiRegistry.panels.findIndex(entry => entry.id === id)
        }

        function init() {
            nowPlayingProbe.addFixture("panel", "panel/now-playing",
                                       "qrc:/qt/qml/Auralis/Ui/shell/NowPlayingPanel.qml", 10)
            fakePlayback.reset(3, 1)
            ShellState.panelOpen = true
            item("sidePanel").currentIndex = panelIndex("panel/now-playing")
            verify(Navigator.open("settings"))
            shell.forceActiveFocus()
            tryVerify(() => findChild(shell, "nowPlayingContent") !== null
                      && findChild(shell, "nowPlayingContent").visible)
            waitForRendering(shell)
        }

        function test_a_without_modules() {
            const cover = item("nowPlayingCover")
            verify(!cover.opensFullscreen)
            compare(cover.hoverCursor, Qt.ArrowCursor)
            compare(Navigator.fullscreenPage, "")
            mouseClick(cover)
            verify(!Navigator.fullscreen)
            keyClick(Qt.Key_F11)
            verify(!Navigator.fullscreen)
            compare(Navigator.currentPage, "settings")

            // 沒有區塊：讀數下方的髮絲線之後什麼都沒有
            compare(findChild(shell, "fixtureNowPlayingSection"), null)
            bareHeight = item("nowPlayingContent").contentHeight

            // 沒有歌詞面板：Ctrl+L 與 showPanel 不做事（收合的面板也不展開）
            ShellState.panelOpen = false
            keyClick(Qt.Key_L, Qt.ControlModifier)
            verify(!ShellState.panelOpen)
            verify(!Navigator.showPanel("panel/lyrics"))
            verify(!ShellState.panelOpen)
            ShellState.panelOpen = true
            waitForRendering(shell)
            grabImage(shell).save(testTmpDir + "/t-76-now-playing-without-section.png")
        }

        function test_b_section_and_lyrics_tab() {
            nowPlayingProbe.addFixture("panel", "panel/lyrics", "SettingsSectionFixture.qml", 20)
            nowPlayingProbe.addFixture("nowPlaying", "test/broken", "NoSuchNowPlayingSection.qml", 10)
            nowPlayingProbe.addFixture("nowPlaying", "test/preview", "NowPlayingSectionFixture.qml", 20)
            tryVerify(() => findChild(shell, "fixtureNowPlayingSection") !== null)
            const section = item("fixtureNowPlayingSection")
            const readouts = item("nowPlayingReadouts")
            waitForRendering(shell)
            // 在讀數（與其後的髮絲線）下方；載不到的那塊不佔位，也沒有多出髮絲線
            verify(section.mapToItem(shell, 0, 0).y > readouts.mapToItem(shell, 0, readouts.height).y)
            verify(section.height > 0)
            compare(item("nowPlayingContent").contentHeight, bareHeight + Theme.space2 + section.height)
            compare(section.width, item("nowPlayingCover").width)
            waitForRendering(shell)
            grabImage(shell).save(testTmpDir + "/t-76-now-playing-with-section.png")

            // 點預覽區塊：切到歌詞頁籤
            mouseClick(section)
            compare(item("sidePanel").currentIndex, panelIndex("panel/lyrics"))

            // Ctrl+L：收合時先展開，再切到歌詞
            item("sidePanel").currentIndex = panelIndex("panel/now-playing")
            ShellState.panelOpen = false
            keyClick(Qt.Key_L, Qt.ControlModifier)
            verify(ShellState.panelOpen)
            compare(item("sidePanel").currentIndex, panelIndex("panel/lyrics"))
        }

        function test_c_fullscreen() {
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenPageFixture.qml", 950)
            compare(Navigator.fullscreenPage, "test/fullscreen")
            const cover = item("nowPlayingCover")
            tryVerify(() => cover.opensFullscreen)
            compare(cover.hoverCursor, Qt.PointingHandCursor)

            // 點封面進入；全螢幕頁拿到焦點，空白鍵與左右鍵由它處理（播放狀態不變）
            const state = fakePlayback.state
            mouseClick(cover)
            verify(Navigator.fullscreen)
            compare(Navigator.currentPage, "test/fullscreen")
            tryVerify(() => findChild(shell, "fixtureFullscreenPage") !== null)
            const page = item("fixtureFullscreenPage")
            tryVerify(() => page.activeFocus)
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Right)
            compare(page.spaces, 1)
            compare(page.arrows, 2)
            compare(fakePlayback.state, state)

            // 頁面自己用掉的 Esc 不離開；沒用掉的 Esc 離開、回到原頁
            page.holdEscape = true
            keyClick(Qt.Key_Escape)
            verify(Navigator.fullscreen)
            keyClick(Qt.Key_Escape)
            verify(!Navigator.fullscreen)
            compare(Navigator.currentPage, "settings")

            // F11 進入、再按一次離開
            shell.forceActiveFocus()
            keyClick(Qt.Key_F11)
            verify(Navigator.fullscreen)
            keyClick(Qt.Key_F11)
            verify(!Navigator.fullscreen)
            compare(Navigator.currentPage, "settings")
            // 離開後按鍵回到殼
            tryVerify(() => shell.activeFocus)
        }
    }
}
