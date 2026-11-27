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

    // 最小視窗 960×640 時頂列搜尋框只剩約 95 px：提示文字要在框內截斷（T-110）
    SearchField {
        id: narrowSearch
        x: 450
        y: 100
        width: 95
        placeholderText: "搜尋曲名、藝人、專輯"
        findShortcut: false
    }

    SignalSpy { id: accepted; target: textField; signalName: "accepted" }
    SignalSpy { id: radio3Selected; target: radio3; signalName: "selected" }
    SignalSpy { id: selectActivated; target: select; signalName: "activated" }

    // 個別選項停用的 Select（介面縮放，AC-19-4）：中間與最後兩個不能選
    Select {
        id: scaleSelect
        x: 450
        y: 200
        label: "介面縮放"
        model: [{ text: "跟隨系統（150%）" }, { text: "100%" }, { text: "125%", enabled: false, reason: "這個螢幕放不下" },
                { text: "150%" }, { text: "175%", enabled: false, reason: "這個螢幕放不下" },
                { text: "200%", enabled: false, reason: "這個螢幕放不下" }]
    }
    SignalSpy { id: scaleActivated; target: scaleSelect; signalName: "activated" }

    // 頭尾都停用、開頭字母各不相同的 Select：Home、End、字元搜尋、確認鍵放開（T-125）
    Select {
        id: edgeSelect
        x: 450
        y: 440
        label: "取樣率"
        model: [{ text: "Auto", enabled: false, reason: "裝置不支援" }, { text: "Bypass" }, { text: "Coarse" },
                { text: "Dither", enabled: false, reason: "裝置不支援" }]
    }
    SignalSpy { id: edgeActivated; target: edgeSelect; signalName: "activated" }

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

        // T-110（P-47）：框太窄時提示文字截斷在框內，寬的時候完整顯示
        function test_placeholder_stays_inside_narrow_field() {
            const narrow = findChild(narrowSearch, "searchPlaceholder")
            verify(narrow !== null)
            verify(narrow.visible)
            verify(narrow.x + narrow.width <= narrowSearch.width - narrowSearch.rightPadding,
                   "提示文字右緣 " + (narrow.x + narrow.width) + " 超出框內 " + (narrowSearch.width - narrowSearch.rightPadding))
            verify(narrow.truncated, "窄框要截斷")
            const wide = findChild(search, "searchPlaceholder")
            search.text = ""
            verify(!wide.truncated, "300 px 寬時完整顯示")
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

        function delegateAt(control, index) {
            const list = control.popup.contentItem
            return list.itemAtIndex(index)
        }

        function test_disabled_options_skipped_by_keys_and_mouse() {
            scaleActivated.clear()
            scaleSelect.currentIndex = 1
            scaleSelect.forceActiveFocus(Qt.TabFocusReason)
            // 清單關著：下鍵跳過停用的 125%，直接到 150%；再往下沒有可用的就不動
            keyClick(Qt.Key_Down)
            compare(scaleSelect.currentIndex, 3)
            compare(scaleActivated.count, 1)
            keyClick(Qt.Key_Down)
            compare(scaleSelect.currentIndex, 3)
            compare(scaleActivated.count, 1)
            keyClick(Qt.Key_Up)
            compare(scaleSelect.currentIndex, 1)

            // 清單開著：停用的選項文字接原因、用 inkFaint；上下鍵醒目列也跳過它
            keyClick(Qt.Key_Space)
            tryCompare(scaleSelect.popup, "opened", true)
            // 清單剛開時高度還沒長開，長高後新排出的選項要等下一次排版才點得到；
            // Release 跑得快，不等的話下面的點擊在排版前送出、落空（T-123）
            verify(waitForPolish(scaleSelect.Window.window))
            const disabled = delegateAt(scaleSelect, 2)
            verify(disabled !== null)
            verify(!disabled.enabled)
            compare(disabled.text, "125%（這個螢幕放不下）")
            compare(disabled.contentItem.color, Theme.inkFaint)
            compare(delegateAt(scaleSelect, 1).contentItem.color, Theme.ink)
            keyClick(Qt.Key_Down)
            compare(scaleSelect.highlightedIndex, 3)
            keyClick(Qt.Key_Down)
            compare(scaleSelect.highlightedIndex, 3)

            // 滑鼠點停用的選項：不選、清單不關
            mouseClick(delegateAt(scaleSelect, 4))
            compare(scaleSelect.currentIndex, 1)
            verify(scaleSelect.popup.visible)
            // 醒目列被滑鼠移到停用的選項上時，Enter 也不選它
            mouseMove(delegateAt(scaleSelect, 4))
            if (scaleSelect.highlightedIndex === 4) {
                keyClick(Qt.Key_Return)
                compare(scaleSelect.currentIndex, 1)
            }
            // 點可用的選項照常選
            mouseClick(delegateAt(scaleSelect, 3))
            tryCompare(scaleSelect.popup, "visible", false)
            compare(scaleSelect.currentIndex, 3)
            compare(scaleSelect.displayText, "150%")
        }

        function test_home_end_search_and_release_skip_disabled() {
            edgeActivated.clear()
            edgeSelect.currentIndex = 1
            edgeSelect.forceActiveFocus(Qt.TabFocusReason)
            // 清單關著：End、Home 停在最後、最前一個可用的；打停用選項的開頭字不選它
            keyClick(Qt.Key_End)
            compare(edgeSelect.currentIndex, 2)
            keyClick(Qt.Key_Home)
            compare(edgeSelect.currentIndex, 1)
            keyClick("d")
            compare(edgeSelect.currentIndex, 1)
            keyClick("a")
            compare(edgeSelect.currentIndex, 1)
            keyClick("c")
            compare(edgeSelect.currentIndex, 2)
            for (let i = 0; i < edgeActivated.count; ++i)
                verify(edgeSelect.optionEnabled(edgeActivated.signalArguments[i][0]))

            // 清單開著：醒目列也一樣
            keyClick(Qt.Key_Space)
            tryCompare(edgeSelect.popup, "opened", true)
            verify(waitForPolish(edgeSelect.Window.window))
            keyClick(Qt.Key_End)
            compare(edgeSelect.highlightedIndex, 2)
            keyClick(Qt.Key_Home)
            compare(edgeSelect.highlightedIndex, 1)
            keyClick("a")
            compare(edgeSelect.highlightedIndex, 1)
            keyClick(Qt.Key_Escape)
            tryCompare(edgeSelect.popup, "visible", false)
            compare(edgeSelect.currentIndex, 2)

            // 目前值本身停用（例：換了螢幕）時清單一開醒目的就是它：確認鍵按下、放開都不選它、清單不關
            edgeSelect.currentIndex = 3
            edgeActivated.clear()
            keyClick(Qt.Key_Space)
            tryCompare(edgeSelect.popup, "opened", true)
            compare(edgeSelect.highlightedIndex, 3)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            verify(edgeSelect.popup.visible)
            keyPress(Qt.Key_Space)
            keyRelease(Qt.Key_Space)
            verify(edgeSelect.popup.visible)
            compare(edgeActivated.count, 0)
            keyClick(Qt.Key_Escape)
            tryCompare(edgeSelect.popup, "visible", false)
            compare(edgeSelect.currentIndex, 3)
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
