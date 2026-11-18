import QtQuick
import QtQuick.Templates as T
import QtTest
import Auralis.Ui

// 介面規範 §10 待驗證 2：只用 QtQuick.Templates 的共用元件在 Basic 樣式下的鍵盤與焦點行為。
Item {
    id: root
    width: 800
    height: 600

    Button {
        id: trigger
        text: "開啟對話框"
    }

    Dialog {
        id: dialog
        variant: "confirm"
        title: "刪除播放清單「通勤」"
        message: "只刪除播放清單，不會刪除音樂檔。"
        confirmText: "刪除播放清單"
    }

    // Templates 的 Menu 沒有內建的 contentItem，要自己給（共用元件 Menu 也會這樣做）
    T.Menu {
        id: menu
        contentItem: ListView {
            implicitWidth: 200
            implicitHeight: contentHeight
            model: menu.contentModel
            currentIndex: menu.currentIndex
        }
        T.MenuItem { text: "接著播放" }
        T.MenuItem { text: "加到佇列最後" }
    }

    SignalSpy { id: accepted; target: dialog; signalName: "accepted" }
    SignalSpy { id: clicked; target: trigger; signalName: "clicked" }

    TestCase {
        name: "Templates"
        when: windowShown

        function focusText() {
            const item = root.Window.activeFocusItem
            return item && item.text !== undefined ? item.text : ""
        }

        function test_button_space_and_enter() {
            clicked.clear()
            trigger.forceActiveFocus(Qt.TabFocusReason)
            verify(trigger.visualFocus)
            keyClick(Qt.Key_Space)
            compare(clicked.count, 1)
        }

        function test_dialog_traps_focus_and_returns_it() {
            trigger.forceActiveFocus(Qt.TabFocusReason)
            dialog.open()
            tryCompare(dialog, "opened", true)
            // 破壞性確認的預設焦點在「取消」
            tryVerify(() => focusText() === "取消", 1000, "預設焦點：" + focusText())
            keyClick(Qt.Key_Tab)
            compare(focusText(), "刪除播放清單")
            // 焦點鎖在對話框裡：再按 Tab 不會跑到外面的按鈕
            for (let i = 0; i < 4; ++i) {
                keyClick(Qt.Key_Tab)
                verify(focusText() === "取消" || focusText() === "刪除播放清單",
                       "焦點跑出對話框：" + focusText())
            }
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "visible", false)
            tryCompare(root.Window, "activeFocusItem", trigger)
        }

        function test_dialog_confirm_with_keyboard() {
            accepted.clear()
            dialog.open()
            tryCompare(dialog, "opened", true)
            tryVerify(() => focusText() === "取消")
            keyClick(Qt.Key_Tab)
            keyClick(Qt.Key_Space)
            tryCompare(accepted, "count", 1)
            tryCompare(dialog, "visible", false)
        }

        function test_menu_arrow_keys_and_escape() {
            menu.open()
            tryCompare(menu, "opened", true)
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 0)
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 1)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
        }
    }
}
