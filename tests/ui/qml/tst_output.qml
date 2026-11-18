import QtQuick
import QtTest
import Auralis.Ui

// 輸出按鈕（S-02）與輸出面板（S-14）：在真的主視窗殼裡用鍵盤走 Ctrl+O 開關、數字鍵與方向鍵選裝置、
// Esc 關閉後焦點回到輸出按鈕；被佔用的裝置不切換並寫原因；輸出狀態與裝置錯誤的顯示（AC-02-3、4、6）。
// Output 背後是假的 PlaybackService（fakePlayback）與假的輸出清單（fakeOutputs）。
Item {
    id: root
    width: 1440
    height: 900

    AppShell {
        id: shell
        anchors.fill: parent
        focus: true
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    TestCase {
        name: "Output"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function selectedRow() {
            return Output.devices.findIndex(row => row.selected)
        }

        function init() {
            Output.panelOpen = false
            fakeOutputs.reset()
            fakePlayback.setOutput("", [], "")
            shell.forceActiveFocus()
        }

        function test_status_on_button() {
            const tag = item("outputStatusTag")
            compare(tag.variant, "idle")
            fakePlayback.setOutput("WASAPI 獨占", [], "")
            compare(tag.text, "位元完美")
            compare(tag.variant, "amber")
            compare(item("outputFormatText").text, "96 kHz / 24 bit")
            fakePlayback.setOutput("WASAPI 共享", ["共享模式"], "")
            compare(tag.text, "共享模式")
            compare(tag.variant, "neutral")
            // 裝置錯誤：signal 標籤寫原因，通知列帶「重試」
            fakePlayback.setOutput("", [], "裝置被其他程式使用中")
            compare(tag.text, "裝置被其他程式使用中")
            compare(tag.variant, "signal")
            compare(Notifier.current.actionText, "重試")
            verify(Notifier.current.text.indexOf("裝置被其他程式使用中") >= 0)
            Notifier.dismiss(Notifier.current.id)
        }

        function test_keyboard() {
            keyClick(Qt.Key_O, Qt.ControlModifier)
            verify(Output.panelOpen)
            const panel = item("outputPanel")
            tryVerify(() => panel.visible)
            compare(Output.devices.length, 3)
            // 數字鍵直接選第 n 列
            keyClick(Qt.Key_3)
            compare(fakeOutputs.selected().slice(-1)[0], "sh1")
            compare(selectedRow(), 2)
            // 方向鍵在列之間移動並選取
            // 面板的列在 Popup 的浮層裡，從視窗的焦點元件確認
            tryVerify(() => shell.Window.activeFocusItem
                      && shell.Window.activeFocusItem.objectName === "outputRow2")
            keyClick(Qt.Key_Up)
            compare(fakeOutputs.selected().slice(-1)[0], "ex2")
            compare(selectedRow(), 1)
            // Esc 關閉，焦點回到輸出按鈕
            keyClick(Qt.Key_Escape)
            verify(!Output.panelOpen)
            tryVerify(() => item("outputButton").activeFocus)
            // 焦點在按鈕上時 Enter 打開、Ctrl+O 關閉
            keyClick(Qt.Key_Return)
            verify(Output.panelOpen)
            keyClick(Qt.Key_O, Qt.ControlModifier)
            verify(!Output.panelOpen)
        }

        function test_busy_device_keeps_output() {
            keyClick(Qt.Key_O, Qt.ControlModifier)
            keyClick(Qt.Key_1)
            compare(selectedRow(), 0)
            fakeOutputs.setBusy("ex2")
            keyClick(Qt.Key_2)
            compare(selectedRow(), 0)
            compare(Output.devices[1].note, "裝置被其他程式使用中")
            compare(Output.devices[1].noteTone, "signal")
            keyClick(Qt.Key_Escape)
        }

        function test_panel_fits_small_window() {
            // 視窗矮到放不下整個面板時（960×640 加上模組的區塊），裝置清單讓出高度、其餘捲動，
            // 面板頂端（標題「輸出」）仍在視窗內
            keyClick(Qt.Key_O, Qt.ControlModifier)
            const scope = item("outputPanel")
            tryVerify(() => scope.visible && scope.height > 0)
            const list = item("outputDeviceList")
            const full = list.height
            const row = full / Output.devices.length
            const others = scope.height - full
            const below = root.height - item("outputButton").mapToItem(shell, 0, 0).y
            keyClick(Qt.Key_Escape)
            root.height = Math.round(others + 2 * Theme.space2 + 2.5 * row + below)
            try {
                keyClick(Qt.Key_O, Qt.ControlModifier)
                tryVerify(() => scope.visible)
                tryVerify(() => list.height < full, 5000, "裝置清單讓出高度：" + list.height)
                verify(list.height >= 2 * Theme.space12, "至少留 2 列：" + list.height)
                tryVerify(() => scope.mapToItem(shell, 0, 0).y >= 0, 5000,
                          "面板頂端在視窗內：" + scope.mapToItem(shell, 0, 0).y)
                keyClick(Qt.Key_Escape)
            } finally {
                root.height = 900
            }
        }

        function test_focused_row_revealed_after_list_resizes() {
            // 打開時清單還沒排好就捲過（例：還很矮時捲到焦點列），排好（高度改變）後焦點列要完整露出
            // 視窗矮到清單只放得下 2.5 列（3 列要捲動）
            keyClick(Qt.Key_O, Qt.ControlModifier)
            const scope = item("outputPanel")
            const list = item("outputDeviceList")
            tryVerify(() => scope.visible && list.height > 0)
            const row = list.height / Output.devices.length
            const others = scope.height - list.height
            const below = root.height - item("outputButton").mapToItem(shell, 0, 0).y
            keyClick(Qt.Key_Escape)
            root.height = Math.round(others + 2 * Theme.space2 + 2.5 * row + below)
            try {
                keyClick(Qt.Key_O, Qt.ControlModifier)
                tryVerify(() => scope.visible && list.height < list.contentHeight)
                keyClick(Qt.Key_1)
                tryVerify(() => shell.Window.activeFocusItem
                          && shell.Window.activeFocusItem.objectName === "outputRow0")
                // 焦點列（第一列）被捲掉一部分，之後清單高度又變了（排好）：焦點列要完整露出
                list.contentY = 10
                list.maxHeight = list.height - 1
                tryCompare(list, "contentY", 0)
                keyClick(Qt.Key_Escape)
            } finally {
                root.height = 900
            }
        }

        // 按 Tab 直到焦點元件符合 match（面板與設定頁的列都可能在浮層裡，從視窗的焦點元件確認）
        function tabUntil(match, what) {
            for (let i = 0; i < 30; ++i) {
                const focused = shell.Window.activeFocusItem
                if (focused && match(focused))
                    return focused
                keyClick(Qt.Key_Tab)
            }
            verify(false, "Tab 到得了 " + what)
        }

        function test_settings_output_section() {
            // 輸出面板的「輸出進階設定」開設定頁、停在「輸出」分區（US-02 設定頁的輸出裝置選擇）
            keyClick(Qt.Key_O, Qt.ControlModifier)
            tryVerify(() => item("outputPanel").visible)
            tabUntil(focused => focused.objectName === "outputAdvanced", "輸出進階設定")
            keyClick(Qt.Key_Return)
            tryVerify(() => !Output.panelOpen)
            compare(Navigator.currentPage, "settings")
            compare(Navigator.params.section, "output")
            tryVerify(() => findChild(shell, "settingsOutputRow2") !== null)
            const section = item("settingsOutput")
            const top = section.mapToItem(shell, 0, 0).y
            verify(top >= 0 && top < shell.height / 2, "輸出分區在畫面上方：" + top)

            // 狀態列的說明（記住的裝置不在時），使用者選了輸出後清掉
            Output.notice = "記住的輸出裝置「Spring 3」（WASAPI 獨占）不在，已改用預設裝置。"
            const notice = item("outputNotice")
            verify(notice.visible)
            compare(notice.text, Output.notice)

            // 鍵盤：Tab 到選取中的那列（還沒選過時是第一列），上下鍵換裝置與模式、立即生效
            tabUntil(focused => focused.objectName === "settingsOutputRow0", "輸出分區的裝置列")
            keyClick(Qt.Key_Down)
            compare(fakeOutputs.selected().slice(-1)[0], "ex2")
            compare(selectedRow(), 1)
            verify(!notice.visible)
            keyClick(Qt.Key_Down)
            compare(fakeOutputs.selected().slice(-1)[0], "sh1")
            compare(selectedRow(), 2)
            verify(item("settingsOutputRow2").checked)
            // 和輸出面板是同一份選擇
            keyClick(Qt.Key_O, Qt.ControlModifier)
            tryVerify(() => shell.Window.activeFocusItem
                      && shell.Window.activeFocusItem.objectName === "outputRow2")
            keyClick(Qt.Key_Escape)
        }

        function test_mouse_toggle() {
            const button = item("outputButton")
            mouseClick(button)
            verify(Output.panelOpen)
            // 再按一次按鈕本身是關閉，不是「點外面關閉後又打開」
            mouseClick(button)
            verify(!Output.panelOpen)
            mouseClick(button)
            verify(Output.panelOpen)
            // 點外面關閉
            mouseClick(root, 200, 200)
            tryVerify(() => !Output.panelOpen)
        }
    }
}
