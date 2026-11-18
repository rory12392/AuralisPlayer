import QtQuick
import QtTest
import Auralis.Ui

// 進出全螢幕不畫出跑版的一格（修正：P-LYR-11，US-17）：只在 windows 平台跑（offscreen 沒有真的視窗
// 狀態與原生視窗大小，略過）。主視窗和 src/app/Main.qml 同一個寫法（Window＋WindowStateKeeper＋
// AppShell），frameProbe 在每一格同步完時記下排版尺寸、原生視窗工作區尺寸、目前頁與 Theme.dark；
// 每一格只能是「一般頁＋原尺寸＋使用者主題」或「全螢幕頁＋全螢幕尺寸＋Void」，排版尺寸也要等於
// 原生視窗的尺寸（不然畫面會被拉伸或露出沒畫到的地方）。
Item {
    id: root
    width: 200
    height: 200

    Component {
        id: windowComponent
        Window {
            id: win
            property alias shell: shell
            property alias keeper: keeper
            width: 960
            height: 640
            // 最上層：測試從背景行程開視窗時系統不讓它到前景，被別的視窗蓋住時 Qt 不畫新的格子
            // （只在改大小時畫一格），逐格紀錄就不準
            flags: Qt.Window | Qt.WindowStaysOnTopHint
            minimumWidth: Theme.windowMinWidth
            minimumHeight: Theme.windowMinHeight
            color: Theme.bg
            WindowStateKeeper {
                id: keeper
                target: win
                fullscreen: Navigator.fullscreen
            }
            AppShell {
                id: shell
                anchors.fill: parent
                focus: true
                playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
            }
        }
    }

    TestCase {
        name: "FullscreenTransition"
        when: windowShown

        property string originalMode
        property bool savedMaximized
        property int savedWidth
        property int savedHeight

        function initTestCase() {
            if (Qt.platform.pluginName !== "windows")
                skip("只在 windows 平台跑（ctest -L window：window.fullscreen_transition）")
            originalMode = ThemeController.mode
            savedMaximized = ShellState.maximized
            savedWidth = ShellState.windowWidth
            savedHeight = ShellState.windowHeight
            nowPlayingProbe.addFixture("fullscreen", "test/fullscreen", "FullscreenPageFixture.qml", 950)
        }

        function cleanupTestCase() {
            frameProbe.detach()
            ThemeController.overrideMode(originalMode)
            ShellState.maximized = savedMaximized
            ShellState.windowWidth = savedWidth
            ShellState.windowHeight = savedHeight
        }

        function describe(f) {
            return f.label + (f.covered ? " 遮罩" : "") + " 頁=" + (f.fullscreenPage ? "全螢幕" : "一般")
                + " 排版=" + f.sceneW + "x" + f.sceneH
                + " 原生=" + f.clientW + "x" + f.clientH + " 螢幕=" + f.screenW + "x" + f.screenH
                + " visibility=" + f.visibility + " dark=" + f.dark + " 已交換=" + f.swapped
        }

        // 等到記到至少 count 格、而且最後一格已經是 done(f) 的狀態
        function waitFrames(win, count, done) {
            for (let i = 0; i < 200; ++i) {
                win.update()
                wait(16)
                const frames = frameProbe.frames()
                if (frames.length >= count && done(frames[frames.length - 1]))
                    return frames
            }
            return frameProbe.frames()
        }

        // 每一格都要是一致的狀態（遮罩蓋著的格子是整片底色，不算跑版）；原生視窗改大小時，系統在新的
        // 一格畫出來之前沿用上一格（第 0 格是換狀態前的最後一格），所以改大小前的那一格要是遮罩。
        // 回傳不一致的格子說明
        function check(frames, normalW, normalH) {
            const bad = []
            for (let i = 0; i < frames.length; ++i) {
                const f = frames[i]
                const fullSize = f.clientW === f.screenW && f.clientH === f.screenH
                const normalSize = f.clientW === normalW && f.clientH === normalH
                const sceneMatches = f.sceneW === f.clientW && f.sceneH === f.clientH
                const next = i + 1 < frames.length ? frames[i + 1] : null
                const resizedAfter = next !== null && (next.clientW !== f.clientW || next.clientH !== f.clientH)
                const ok = f.covered ? true
                    : !resizedAfter && sceneMatches && (f.fullscreenPage ? fullSize && f.dark : normalSize && !f.dark)
                console.log("格 " + i + "：" + describe(f) + (ok ? "" : "  ← 跑版"))
                if (!ok)
                    bad.push(i + "：" + describe(f))
            }
            return bad
        }

        function test_enter_and_leave_without_mixed_frame() {
            ThemeController.overrideMode("paper")
            ShellState.maximized = false
            ShellState.windowWidth = 960
            ShellState.windowHeight = 640
            const win = createTemporaryObject(windowComponent, root)
            verify(win !== null)
            win.keeper.restore()
            win.visible = true
            verify(Navigator.open("settings"))
            tryVerify(() => win.visibility === Window.Windowed && win.width === 960, 3000)
            frameProbe.attach(win, win.shell)
            frameProbe.mark("穩定")
            waitFrames(win, 2, f => true)
            const normalW = win.width
            const normalH = win.height

            frameProbe.mark("進入")
            verify(Navigator.enterFullscreen())
            const enter = waitFrames(win, 4, f => !f.covered && f.fullscreenPage && f.clientW === f.screenW
                                                  && f.sceneW === f.screenW)
            const enterBad = check(enter, normalW, normalH)

            frameProbe.mark("離開")
            verify(Navigator.leaveFullscreen())
            const leave = waitFrames(win, 4, f => !f.covered && !f.fullscreenPage && f.clientW === normalW
                                                  && f.sceneW === normalW)
            const leaveBad = check(leave, normalW, normalH)

            frameProbe.detach()
            compare(win.visibility, Window.Windowed)
            verify(enter.length > 0 && enter[enter.length - 1].fullscreenPage, "進入後最後一格是全螢幕頁")
            verify(leave.length > 0 && !leave[leave.length - 1].fullscreenPage, "離開後最後一格是一般頁")
            compare(enterBad.join("\n"), "", "進全螢幕時畫出跑版的格子")
            compare(leaveBad.join("\n"), "", "離開全螢幕時畫出跑版的格子")
        }
    }
}
