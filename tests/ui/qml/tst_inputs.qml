import QtQuick
import QtTest
import Auralis.Ui

// 共用元件的鍵盤與焦點（介面規範 §3、§7）：TextField、SearchField、RadioRow、Select、Slider。
Item {
    id: root
    width: 800
    height: 600

    function focusItem() {
        return root.Window.activeFocusItem
    }

    Column {
        spacing: 8
        width: 400
        Button { id: start; text: "起點" }
        TextField { id: textField; width: 300; label: "播放清單名稱"; placeholderText: "輸入名稱" }
        SearchField { id: search; width: 300; placeholderText: "搜尋曲名、藝人、專輯" }
        Column {
            id: radios
            width: 400
            RadioRow { id: radio1; width: 400; text: "WASAPI 獨占"; detail: "USB DAC"; checked: true }
            RadioRow { id: radio2; width: 400; text: "ASIO"; unavailableReason: "沒有 ASIO 驅動" }
            RadioRow { id: radio3; width: 400; text: "WASAPI 共享"; detail: "耳機" }
        }
        Select { id: select; label: "排序"; model: ["專輯藝人", "年份", "加入時間"] }
        Slider { id: slider; width: 300; label: "音量"; from: 0; to: 1; stepSize: 0.1; value: 0.5 }
    }

    // 被限制寬度的 Select：值要在箭頭前截斷（T-92）
    Select {
        id: narrowSelect
        x: 450
        width: 140
        label: "排序"
        model: ["專輯藝人（依年份與光碟編號）"]
    }

    SignalSpy { id: accepted; target: textField; signalName: "accepted" }
    SignalSpy { id: radio3Selected; target: radio3; signalName: "selected" }
    SignalSpy { id: selectActivated; target: select; signalName: "activated" }

    TestCase {
        name: "TextField"
        when: windowShown

        function test_tab_label_error_and_enter() {
            accepted.clear()
            start.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), textField.input)
            compare(textField.input.Accessible.name, "播放清單名稱")
            keyClick(Qt.Key_A)
            compare(textField.text, "a")
            keyClick(Qt.Key_Return)
            compare(accepted.count, 1)
            textField.errorText = "已經有叫「a」的播放清單"
            compare(textField.input.Accessible.description, "已經有叫「a」的播放清單")
            textField.errorText = ""
            textField.text = ""
        }
    }

    TestCase {
        name: "SearchField"
        when: windowShown

        function test_ctrl_f_focus_and_escape_clears() {
            start.forceActiveFocus(Qt.TabFocusReason)
            search.text = "Bach"
            keySequence(StandardKey.Find)
            compare(root.focusItem(), search)
            compare(search.selectedText, "Bach")
            keyClick(Qt.Key_Escape)
            compare(search.text, "")
            compare(search.Accessible.name, "搜尋曲名、藝人、專輯")
        }

        // T-29（S-07）：有字時就畫 amber 底線，不只是有焦點時
        function test_amber_underline_when_text_or_focus() {
            const underline = findChild(search, "searchUnderline")
            verify(underline !== null)
            start.forceActiveFocus(Qt.TabFocusReason)
            search.text = ""
            verify(!underline.visible, "沒字、沒焦點時不畫")
            search.text = "Bach"
            verify(underline.visible, "有字時要畫")
            search.text = ""
            search.forceActiveFocus(Qt.TabFocusReason)
            verify(underline.visible, "有焦點時要畫")
            start.forceActiveFocus(Qt.TabFocusReason)
        }

        function test_tab_order_skips_clear_button() {
            search.text = "x"
            textField.input.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), search)
            keyClick(Qt.Key_Tab)
            verify(root.focusItem() !== search && root.focusItem().iconName !== "clear", "清除鈕不在 Tab 順序裡")
            search.text = ""
        }
    }

    TestCase {
        name: "RadioRow"
        when: windowShown

        function test_tab_reaches_checked_and_arrows_skip_unavailable() {
            radio3Selected.clear()
            radio1.checked = true
            search.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), radio1)
            compare(radio1.Accessible.name, "WASAPI 獨占 USB DAC")
            keyClick(Qt.Key_Down)
            // radio2 不可用，直接跳到 radio3
            compare(root.focusItem(), radio3)
            verify(radio3.checked)
            verify(!radio1.checked)
            compare(radio3Selected.count, 1)
            compare(radio2.Accessible.description, "沒有 ASIO 驅動")
            keyClick(Qt.Key_Up)
            verify(radio1.checked)
            // 只有選取的那列在 Tab 順序裡
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), select)
        }
    }

    TestCase {
        name: "Select"
        when: windowShown

        function test_arrow_changes_and_space_opens() {
            selectActivated.clear()
            select.currentIndex = 0
            select.forceActiveFocus(Qt.TabFocusReason)
            compare(select.Accessible.name, "排序")
            keyClick(Qt.Key_Down)
            compare(select.currentIndex, 1)
            compare(selectActivated.count, 1)
            keyClick(Qt.Key_Space)
            tryCompare(select.popup, "opened", true)
            verify(select.popup.width > 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(select.popup, "visible", false)
            compare(select.currentIndex, 2)
            keyClick(Qt.Key_Space)
            tryCompare(select.popup, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(select.popup, "visible", false)
            compare(root.focusItem(), select)
        }

        function findValue(control) {
            for (let i = 0; i < control.contentItem.children.length; ++i) {
                if (control.contentItem.children[i].objectName === "selectValue")
                    return control.contentItem.children[i]
            }
            return null
        }

        function test_narrow_value_elides_before_arrow() {
            const value = findValue(narrowSelect)
            verify(value !== null)
            const right = value.mapToItem(narrowSelect, value.width, 0).x
            verify(right <= narrowSelect.indicator.x, "值的右緣 " + right + " 壓到箭頭 " + narrowSelect.indicator.x)
            verify(value.truncated)
            // 放得下的不截斷
            verify(!findValue(select).truncated)
        }
    }

    TestCase {
        name: "Slider"
        when: windowShown

        function test_tab_and_arrow_keys() {
            slider.value = 0.5
            select.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem(), slider)
            compare(slider.Accessible.name, "音量")
            keyClick(Qt.Key_Right)
            fuzzyCompare(slider.value, 0.6, 0.0001)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            fuzzyCompare(slider.value, 0.4, 0.0001)
        }
    }
}
