import QtQuick
import QtTest
import Auralis.Ui

// 顯示類共用元件（介面規範 §3、§6、§6.2）：Icon、Panel、Readout、StatusTag、RingGauge、ProgressLine、
// EmptyState、ErrorBlock、Skeleton、Cover。沒有互動的元件驗證無障礙資訊與狀態；有按鈕的驗證鍵盤。
Item {
    id: root
    width: 800
    height: 600

    function focusItem() {
        return root.Window.activeFocusItem
    }

    Column {
        spacing: 8
        width: 600
        Icon { id: icon; name: "play"; size: 20 }
        Panel {
            id: panel
            width: 400
            title: "輸出"
            index: "Ctrl O"
            Rectangle { id: panelContent; width: 100; height: 120 }
        }
        Readout { id: readout; label: "送到 DAC"; value: "96"; unit: "kHz / 24 bit"; tone: "amber" }
        StatusTag { id: amberTag; text: "位元完美"; variant: "amber" }
        StatusTag { id: idleTag; text: "未連線"; variant: "idle" }
        RingGauge { id: gauge; label: "首次掃描"; value: 0.314 }
        ProgressLine { id: progress; width: 300; label: "掃描"; value: 0.5 }
        Button { id: start; text: "起點" }
        EmptyState { id: empty; width: 400; title: "佇列是空的"; description: "說明"; actionText: "加入音樂資料夾" }
        ErrorBlock { id: errorBlock; width: 400 }
        Skeleton { id: skeleton; width: 300; variant: "row" }
        Cover { id: cover; size: 48; albumName: "沒有封面的專輯" }
    }

    SignalSpy { id: emptyAction; target: empty; signalName: "action" }
    SignalSpy { id: retry; target: errorBlock; signalName: "retry" }

    TestCase {
        name: "Icon"
        when: windowShown

        function test_every_icon_parses() {
            const names = IconLibrary.names()
            // 介面規範 §6.2 的 19 個加上正在播放面板與兩個排序方向
            verify(names.length >= 22, "圖示數量：" + names.length)
            for (const name of ["play", "pause", "previous", "next", "shuffle", "repeat", "repeat-one", "search",
                                "clear", "add", "chevron-up", "chevron-down", "chevron-right", "sort-asc",
                                "sort-desc", "folder", "settings", "fullscreen", "fullscreen-exit", "more",
                                "drag-handle", "warning", "panel"]) {
                verify(names.indexOf(name) >= 0, "缺少圖示：" + name)
                verify(IconLibrary.path(name).startsWith("M"), "路徑讀不出來：" + name)
            }
            // 正圓轉成弧線
            verify(IconLibrary.path("search").indexOf("A") > 0)
            ignoreWarning(new RegExp("沒有這個圖示"))
            compare(IconLibrary.path("no-such-icon"), "")
        }

        function test_size_and_tone_follow_theme() {
            compare(icon.width, 20)
            compare(icon.strokeColor, Theme.ink)
            icon.tone = "muted"
            compare(icon.strokeColor, Theme.inkMuted)
            icon.tone = "ink"
            verify(icon.Accessible.ignored)
        }
    }

    TestCase {
        name: "Panel"
        when: windowShown

        function test_height_follows_content() {
            verify(panel.implicitHeight >= panelContent.height + 2 * Theme.space6)
            const before = panel.implicitHeight
            panelContent.height = 200
            compare(panel.implicitHeight, before + 80)
        }
    }

    TestCase {
        name: "Readout"
        when: windowShown

        function test_accessible_name_reads_value_and_unit() {
            compare(readout.Accessible.name, "送到 DAC 96 kHz / 24 bit")
        }
    }

    TestCase {
        name: "StatusTag"
        when: windowShown

        function test_text_and_glow_only_on_amber() {
            compare(amberTag.Accessible.name, "位元完美")
            // 光暈層只在 amber
            let amberLayers = 0
            for (const child of amberTag.children[0].children)
                if (child.spread !== undefined)
                    ++amberLayers
            compare(amberLayers, amberTag.glowLayers)
            let idleLayers = 0
            for (const child of idleTag.children[0].children)
                if (child.spread !== undefined)
                    ++idleLayers
            compare(idleLayers, 0)
        }
    }

    TestCase {
        name: "RingGauge"
        when: windowShown

        function test_progress_role_and_clamp() {
            compare(gauge.Accessible.role, Accessible.ProgressBar)
            compare(gauge.Accessible.description, "31%")
            gauge.value = 1.7
            compare(gauge.Accessible.description, "100%")
            gauge.value = 0.314
        }
    }

    TestCase {
        name: "ProgressLine"
        when: windowShown

        function test_role_and_indeterminate() {
            compare(progress.Accessible.role, Accessible.ProgressBar)
            compare(progress.Accessible.description, "50%")
            progress.indeterminate = true
            compare(progress.Accessible.description, "")
            progress.indeterminate = false
        }
    }

    TestCase {
        name: "EmptyState"
        when: windowShown

        function test_action_button_by_keyboard() {
            emptyAction.clear()
            start.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "加入音樂資料夾")
            keyClick(Qt.Key_Space)
            compare(emptyAction.count, 1)
        }
    }

    TestCase {
        name: "ErrorBlock"
        when: windowShown

        function test_retry_by_keyboard() {
            retry.clear()
            empty.forceActiveFocus()
            start.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            keyClick(Qt.Key_Tab)
            compare(root.focusItem().text, "重試")
            keyClick(Qt.Key_Return)
            compare(retry.count, 1)
        }
    }

    TestCase {
        name: "Skeleton"
        when: windowShown

        function test_shows_after_delay() {
            skeleton.shown = false
            verify(!skeleton.shown)
            // 介面規範 §6：超過 150 毫秒才顯示
            wait(50)
            verify(!skeleton.shown)
            tryCompare(skeleton, "shown", true, 1000)
        }
    }

    TestCase {
        name: "Cover"
        when: windowShown

        function test_placeholder_without_image() {
            verify(!cover.hasImage)
            verify(!cover.loading)
            compare(cover.Accessible.name, "沒有封面的專輯")
            ignoreWarning(new RegExp("Cannot open"))
            cover.source = "file:///no/such/cover.jpg"
            tryVerify(() => !cover.loading)
            verify(!cover.hasImage)
        }
    }
}
