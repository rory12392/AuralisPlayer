import QtQuick
import QtTest
import Auralis.Ui

// 浮層與捲動類共用元件（介面規範 §3）：Menu、MenuItem、ToolTip、ScrollBar。
// Dialog 的鎖焦點與 Esc 在 tst_templates.qml。
Item {
    id: root
    width: 800
    height: 600

    function focusItem() {
        return root.Window.activeFocusItem
    }

    Button {
        id: trigger
        text: "開啟選單"
        onClicked: menu.popup(trigger, 0, trigger.height)
    }

    Menu {
        id: menu
        MenuItem { id: playItem; text: "播放"; shortcut: "Enter" }
        MenuItem { text: "從播放清單移除"; enabled: false }
        MenuItem { id: queueItem; text: "加到佇列最後"; shortcut: "Ctrl+Enter" }
        Menu {
            id: subMenu
            title: "加入播放清單"
            MenuItem { text: "通勤" }
        }
    }

    Item {
        id: tipHost
        x: 300
        y: 100
        width: 40
        height: 40
        ToolTip {
            id: toolTip
            text: "DSD 直通時無法調整音量"
            delay: 0
        }
    }

    ListView {
        id: list
        x: 400
        y: 200
        width: 200
        height: 100
        clip: true
        model: 50
        delegate: Item { width: 200; height: 20 }
        ScrollBar.vertical: ScrollBar { id: scrollBar }
    }

    SignalSpy { id: queueTriggered; target: queueItem; signalName: "triggered" }

    TestCase {
        name: "Menu"
        when: windowShown

        function test_arrows_skip_disabled_enter_and_escape() {
            queueTriggered.clear()
            trigger.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryCompare(menu, "opened", true)
            verify(menu.width > 0 && menu.height > 0)
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 0)
            // 停用的項目跳過
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 2)
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false)
            compare(queueTriggered.count, 1)
            // 關閉後焦點回到觸發的按鈕
            tryCompare(root.Window, "activeFocusItem", trigger)
        }

        function test_escape_closes() {
            trigger.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryCompare(menu, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
            tryCompare(root.Window, "activeFocusItem", trigger)
        }
    }

    TestCase {
        name: "MenuItem"
        when: windowShown

        function test_height_shortcut_and_submenu() {
            compare(playItem.height, Theme.rowHeightCompact)
            compare(playItem.shortcut, "Enter")
            compare(playItem.Accessible.name, "播放")
            trigger.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryCompare(menu, "opened", true)
            // 移到「加入播放清單」，右鍵開子選單
            menu.currentIndex = 3
            keyClick(Qt.Key_Right)
            tryCompare(subMenu, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(subMenu, "visible", false)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
        }
    }

    TestCase {
        name: "ToolTip"
        when: windowShown

        function test_has_size_and_closes_on_escape() {
            toolTip.open()
            tryCompare(toolTip, "opened", true)
            // Templates 的 Popup 沒有隱含尺寸，元件自己算，背景才看得到
            verify(toolTip.width > 0 && toolTip.height > 0)
            compare(toolTip.background.width, toolTip.width)
            toolTip.close()
            tryCompare(toolTip, "visible", false)
        }
    }

    TestCase {
        name: "ScrollBar"
        when: windowShown

        function test_attached_to_list_and_keyboard_scroll() {
            // 用 Auralis.Ui 的 ScrollBar 也能掛在 ScrollBar.vertical 上
            compare(scrollBar.orientation, Qt.Vertical)
            verify(scrollBar.size < 1)
            verify(scrollBar.visible)
            compare(scrollBar.width, Theme.space2)
            scrollBar.increase()
            verify(list.contentY > 0)
        }
    }
}
