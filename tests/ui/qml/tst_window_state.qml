import QtQuick
import QtTest
import Auralis.Ui

// 主視窗的最大化與全螢幕（介面規範 §7，T-92）：使用者用系統操作最大化後，ShellState 其他屬性改變
// （開關正在播放面板）或切頁都不會把視窗改回一般大小；全螢幕進出後回到原本的最大化或一般視窗；
// 關閉時記下最大化。視窗狀態用 WindowStateKeeper 管，和主視窗（src/app/Main.qml）同一個寫法。
Item {
    id: root
    width: 200
    height: 200

    Component {
        id: windowComponent
        Window {
            id: win
            property alias keeper: keeper
            property bool fullscreenPage: false
            minimumWidth: 320
            minimumHeight: 240
            WindowStateKeeper {
                id: keeper
                target: win
                fullscreen: win.fullscreenPage
            }
        }
    }

    TestCase {
        name: "WindowState"
        when: windowShown

        property bool savedMaximized
        property bool savedPanelOpen
        property int savedWidth
        property int savedHeight

        function init() {
            savedMaximized = ShellState.maximized
            savedPanelOpen = ShellState.panelOpen
            savedWidth = ShellState.windowWidth
            savedHeight = ShellState.windowHeight
            ShellState.windowWidth = 640
            ShellState.windowHeight = 480
        }

        function cleanup() {
            ShellState.maximized = savedMaximized
            ShellState.panelOpen = savedPanelOpen
            ShellState.windowWidth = savedWidth
            ShellState.windowHeight = savedHeight
        }

        function open() {
            const win = createTemporaryObject(windowComponent, root)
            verify(win !== null)
            win.keeper.restore()
            return win
        }

        function test_system_maximize_survives_shell_state_changes() {
            ShellState.maximized = false
            const win = open()
            tryCompare(win, "visibility", Window.Windowed)
            compare(win.width, 640)
            // 標題列的最大化：不經過 ShellState
            win.showMaximized()
            tryCompare(win, "visibility", Window.Maximized)
            ShellState.panelOpen = !ShellState.panelOpen
            ShellState.panelOpen = !ShellState.panelOpen
            wait(100)
            compare(win.visibility, Window.Maximized)
            // 還原成一般視窗後一樣不被改回
            win.showNormal()
            tryCompare(win, "visibility", Window.Windowed)
            ShellState.maximized = true
            wait(100)
            compare(win.visibility, Window.Windowed)
        }

        function test_restores_remembered_maximized() {
            ShellState.maximized = true
            const win = open()
            tryCompare(win, "visibility", Window.Maximized)
        }

        function test_fullscreen_returns_to_maximized() {
            ShellState.maximized = false
            const win = open()
            win.showMaximized()
            tryCompare(win, "visibility", Window.Maximized)
            win.fullscreenPage = true
            tryCompare(win, "visibility", Window.FullScreen)
            win.fullscreenPage = false
            tryCompare(win, "visibility", Window.Maximized)
            wait(100)
            compare(win.visibility, Window.Maximized)
        }

        function test_fullscreen_returns_to_windowed() {
            ShellState.maximized = false
            const win = open()
            tryCompare(win, "visibility", Window.Windowed)
            win.fullscreenPage = true
            tryCompare(win, "visibility", Window.FullScreen)
            win.fullscreenPage = false
            tryCompare(win, "visibility", Window.Windowed)
            compare(win.width, 640)
        }

        // T-76：全螢幕開在主視窗所在的螢幕。左右兩個螢幕（假的螢幕資訊）時依視窗中心選
        function test_fullscreen_screen_is_window_center() {
            const win = open()
            const screens = [{ name: "左", virtualX: 0, virtualY: 0, width: 1920, height: 1080 },
                             { name: "右", virtualX: 1920, virtualY: -200, width: 2560, height: 1440 }]
            compare(win.keeper.screenAt(screens, 100, 100).name, "左")
            compare(win.keeper.screenAt(screens, 1919, 1079).name, "左")
            compare(win.keeper.screenAt(screens, 1920, -100).name, "右")
            compare(win.keeper.screenAt(screens, 3000, 1200).name, "右")
            compare(win.keeper.screenAt(screens, -10, 100), null)
            // 實際的螢幕：視窗中心所在的那個就是進全螢幕後的螢幕
            win.fullscreenPage = true
            tryCompare(win, "visibility", Window.FullScreen)
            compare(win.screen.name, win.keeper.screenAt(Application.screens,
                    win.screen.virtualX + 1, win.screen.virtualY + 1).name)
            win.fullscreenPage = false
            tryCompare(win, "visibility", Window.Windowed)
        }

        function test_save_remembers_maximized_and_windowed_size() {
            ShellState.maximized = false
            const win = open()
            win.showMaximized()
            tryCompare(win, "visibility", Window.Maximized)
            // 全螢幕中關閉也記成最大化（進全螢幕前的狀態）
            win.fullscreenPage = true
            tryCompare(win, "visibility", Window.FullScreen)
            win.keeper.save()
            verify(ShellState.maximized)
            win.fullscreenPage = false
            tryCompare(win, "visibility", Window.Maximized)
            win.showNormal()
            tryCompare(win, "visibility", Window.Windowed)
            win.width = 700
            win.keeper.save()
            verify(!ShellState.maximized)
            compare(ShellState.windowWidth, 700)
        }
    }
}
