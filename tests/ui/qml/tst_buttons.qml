import QtQuick
import QtTest
import Auralis.Ui

// 共用元件的鍵盤與焦點（介面規範 §3、§7）：Button、IconButton、SegmentedControl、Chip、Checkbox、Tabs。
// 每個元件一個 TestCase；共通的檢查是 Tab 可達、空白鍵與 Enter 可觸發、Accessible.name 有值。
Item {
    id: root
    width: 800
    height: 600

    function focusItem() {
        return root.Window.activeFocusItem
    }

    Column {
        spacing: 8
        Button { id: start; text: "起點" }
        Button { id: button; text: "匯出播放清單" }
        IconButton { id: iconButton; iconName: "play"; text: "播放" }
        SegmentedControl {
            id: segmented
            label: "DSD 輸出"
            model: [{ text: "DoP" }, { text: "原生 DSD", enabled: false, reason: "需選 ASIO" }, { text: "轉 PCM" }]
        }
        Chip { id: chip; label: "格式"; menu: chipMenu }
        Checkbox { id: checkbox; text: "線上抓取歌詞" }
        Tabs { id: tabs; model: ["正在播放", "歌詞", "佇列"] }
    }
    Menu {
        id: chipMenu
        MenuItem { text: "FLAC" }
    }

    SignalSpy { id: buttonClicked; target: button; signalName: "clicked" }
    SignalSpy { id: iconClicked; target: iconButton; signalName: "clicked" }
    SignalSpy { id: segmentActivated; target: segmented; signalName: "activated" }
    SignalSpy { id: chipCleared; target: chip; signalName: "cleared" }

    TestCase {
        name: "Button"
        when: windowShown

        function test_tab_space_enter() {
            buttonClicked.clear()
            start.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), button)
            verify(button.visualFocus)
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_Return)
            compare(buttonClicked.count, 2)
            compare(button.Accessible.name, "匯出播放清單")
        }

        function test_busy_disables_and_restores() {
            buttonClicked.clear()
            button.busy = true
            verify(!button.enabled)
            compare(button.Accessible.description, "處理中")
            mouseClick(button)
            compare(buttonClicked.count, 0)
            button.busy = false
            verify(button.enabled)
            mouseClick(button)
            compare(buttonClicked.count, 1)
        }
    }

    TestCase {
        name: "IconButton"
        when: windowShown

        function test_name_tooltip_and_keys() {
            iconClicked.clear()
            compare(iconButton.Accessible.name, "播放")
            button.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), iconButton)
            keyClick(Qt.Key_Space)
            compare(iconClicked.count, 1)
            // 鍵盤焦點時顯示說明，說明有實際寬度（Templates 的 Popup 沒有隱含尺寸）
            let toolTip = null
            for (const child of iconButton.data) {
                if (child.text === "播放" && child.delay !== undefined)
                    toolTip = child
            }
            verify(toolTip !== null, "IconButton 裡要有 ToolTip")
            tryCompare(toolTip, "opened", true)
            verify(toolTip.width > 0)
        }
    }

    TestCase {
        name: "SegmentedControl"
        when: windowShown

        function test_tab_reaches_selected_and_arrows_skip_disabled() {
            segmentActivated.clear()
            segmented.currentIndex = 0
            iconButton.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "DoP")
            compare(root.focusItem().Accessible.role, Accessible.RadioButton)
            // 「原生 DSD」停用，右鍵直接跳到「轉 PCM」
            keyClick(Qt.Key_Right)
            compare(segmented.currentIndex, 2)
            compare(segmentActivated.count, 1)
            compare(root.focusItem().text, "轉 PCM")
            keyClick(Qt.Key_Left)
            compare(segmented.currentIndex, 0)
            // 選取的那格之外不在 Tab 順序裡：再按 Tab 就離開這個元件
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), chip)
        }

        function test_selected_segment_is_checked_before_any_change() {
            // 無障礙啟用時按鈕用自己的 checked 回報選取（UI Automation 的 IsSelected），
            // 所以一建立就要和選取一致，不能等鍵盤動過
            const segments = segmented.children[0].children
            for (const child of segments) {
                if (child.text === "DoP")
                    verify(child.checked, "預設選中的那格是 checked")
                else if (child.text !== undefined)
                    verify(!child.checked, child.text + " 沒選")
            }
        }

        function test_disabled_option_shows_reason() {
            let found = false
            for (const child of segmented.children[0].children) {
                if (child.text === "原生 DSD（需選 ASIO）") {
                    found = true
                    verify(!child.enabled)
                }
            }
            verify(found, "停用的選項要把原因寫在選項上")
        }
    }

    TestCase {
        name: "Chip"
        when: windowShown

        function test_space_opens_menu_and_delete_clears() {
            chipCleared.clear()
            chip.value = ""
            chip.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryCompare(chipMenu, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(chipMenu, "visible", false)
            // 未套用時 Delete 不做事；套用後 Delete 清除
            keyClick(Qt.Key_Delete)
            compare(chipCleared.count, 0)
            chip.value = "FLAC"
            // T-29：已套用的文案照 DESIGN／S-03「格式 FLAC」
            compare(chip.text, "格式 FLAC")
            compare(chip.Accessible.name, "格式 FLAC")
            chip.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Delete)
            compare(chipCleared.count, 1)
        }
    }

    TestCase {
        name: "Checkbox"
        when: windowShown

        function test_tab_and_space_toggle() {
            checkbox.checked = false
            chip.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), checkbox)
            keyClick(Qt.Key_Space)
            verify(checkbox.checked)
            compare(checkbox.Accessible.name, "線上抓取歌詞")
        }
    }

    TestCase {
        name: "Tabs"
        when: windowShown

        function test_tab_stops_at_current_and_arrows_switch() {
            tabs.currentIndex = 0
            checkbox.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "正在播放")
            compare(root.focusItem().Accessible.role, Accessible.PageTab)
            keyClick(Qt.Key_Right)
            compare(tabs.currentIndex, 1)
            compare(root.focusItem().text, "歌詞")
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right) // 最後一個之後不再往右
            compare(tabs.currentIndex, 2)
            // 頁籤寬度照文字，不互相重疊
            const first = tabs.itemAt(0)
            const second = tabs.itemAt(1)
            tryVerify(() => second.x > 0)
            verify(first.x + first.width <= second.x, "頁籤重疊")
        }
    }
}
