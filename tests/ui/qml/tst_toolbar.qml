import QtQuick
import QtTest
import Auralis.Ui

// 工具列常駐項目（T-28）：UiRegistry.toolbarItems 依 order 放在頁面工具列左邊、每一頁都看得到、
// 全螢幕時不顯示；載不到的項目只讓那一塊不出現，頁面工具列照常；窄視窗（960×640）時頁面工具列仍放得下。
// 登記的項目見 quick_test_main.cpp：窄項目（order 10）、載不到的（15）、搜尋框樣子的（20），
// 另有一頁有自己工具列的頁面 test/toolbar-page 與全螢幕頁 test/toolbar-fullscreen。
// 最後在 1600×900 與 960×640 各截一張（testTmpDir），收尾時搬進 docs/evidence/screenshots/。
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
        name: "Toolbar"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        // 目前頁面的工具列（換頁時舊的那份可能還沒銷毀，所以不用 findChild 找）
        function pageToolbar() {
            const loader = item("pageToolbar")
            verify(loader.item !== null, "頁面工具列沒有載入")
            compare(loader.item.objectName, "fixturePageToolbar")
            return loader.item
        }

        function rightEdge(target) {
            return target.mapToItem(shell, target.width, 0).x
        }

        function leftEdge(target) {
            return target.mapToItem(shell, 0, 0).x
        }

        function init() {
            shell.width = root.width
            shell.height = root.height
            ShellState.panelOpen = true
            verify(Navigator.open("test/toolbar-page"))
            // Row 在下一次畫面更新前才重新排（polish），等它排好再量位置
            waitForRendering(shell)
        }

        function test_items_in_order() {
            const narrow = item("fixtureToolbarNarrow")
            const search = item("fixtureToolbarSearch")
            verify(narrow.visible && search.visible)
            verify(rightEdge(narrow) <= leftEdge(search))
            // 載不到的（order 15）不出現：只剩兩塊佔位
            const row = item("toolbarItems")
            let shown = 0
            for (let i = 0; i < row.children.length; ++i) {
                if (row.children[i].visible && row.children[i].width > 0)
                    ++shown
            }
            compare(shown, 2)
            // 項目照自己的 implicitWidth
            compare(search.width, search.implicitWidth)
        }

        function test_page_toolbar_right_of_items() {
            const page = pageToolbar()
            verify(page.visible, "頁面工具列沒有顯示")
            verify(leftEdge(page) >= rightEdge(item("fixtureToolbarSearch")),
                   "頁面工具列 " + leftEdge(page) + " 和搜尋框 " + rightEdge(item("fixtureToolbarSearch")) + " 重疊")
            compare(Navigator.currentPage, "test/toolbar-page")
        }

        function test_items_on_every_page() {
            verify(Navigator.open("settings"))
            verify(item("fixtureToolbarSearch").visible)
            verify(Navigator.open("test/toolbar-page"))
            verify(item("fixtureToolbarSearch").visible)
        }

        function test_hidden_in_fullscreen() {
            nowPlayingProbe.addFixture("fullscreen", "test/toolbar-fullscreen", "ToolbarPageFixture.qml", 960)
            verify(Navigator.open("test/toolbar-fullscreen"))
            verify(shell.fullscreen)
            const row = item("toolbarItems")
            verify(!row.visible)
            verify(Navigator.back())
            verify(!shell.fullscreen)
            verify(row.visible)
        }

        function test_narrow_window_keeps_page_toolbar() {
            shell.width = 960
            shell.height = 640
            ShellState.panelOpen = false
            waitForRendering(shell)
            const page = pageToolbar()
            const search = item("fixtureToolbarSearch")
            // 頁面工具列整排放得下，不和常駐項目或面板開關重疊
            verify(leftEdge(page) >= rightEdge(search))
            verify(page.implicitWidth <= page.width,
                   "頁面工具列放不下：需要 " + page.implicitWidth + "，只有 " + page.width)
        }

        // T-29（S-07）：頁面的 statusText 在狀態列右邊（等寬），statusTextLeft 在左邊；
        // 掃描進度和頁面文字在同一排右邊並排（進度在最右），不會疊在一起
        function test_status_bar_sides() {
            const right = item("pageStatus")
            const left = item("pageStatusLeft")
            const content = item("pageToolbar")
            compare(right.text, "工具列測試頁")
            verify(right.visible)
            const contentRight = content.mapToItem(shell, content.width, 0).x
            verify(rightEdge(right) > contentRight - Theme.space6 - 2 && rightEdge(right) > shell.width / 2,
                   "頁面狀態文字要在右邊：" + rightEdge(right))
            verify(!left.visible, "沒有 statusTextLeft 時不顯示")
            const scan = item("scanStatus")
            compare(scan.parent, item("statusRight"))
            compare(right.parent, scan.parent)
            // Row 依子項目順序排：頁面文字在前、掃描進度在最右
            const children = item("statusRight").children
            let order = []
            for (let i = 0; i < children.length; ++i)
                order.push(children[i].objectName)
            compare(order.indexOf("pageStatus") + 1, order.indexOf("scanStatus"), order.join(","))
        }

        function test_status_bar_left_text() {
            const pageObj = item("fixturePage")
            const left = item("pageStatusLeft")
            pageObj.statusTextLeft = "已選取 3 首。"
            waitForRendering(shell)
            verify(left.visible)
            compare(left.text, "已選取 3 首。")
            verify(leftEdge(left) < shell.width / 2, "選取說明在左邊")
            verify(rightEdge(left) <= leftEdge(item("pageStatus")), "左右不重疊")
            pageObj.statusTextLeft = ""
        }

        function test_screenshots() {
            waitForRendering(shell)
            // save 失敗時丟例外，測試就失敗
            grabImage(shell).save(testTmpDir + "/t-28-toolbar-1600x900.png")
            shell.width = 960
            shell.height = 640
            ShellState.panelOpen = false
            waitForRendering(shell)
            wait(200)
            grabImage(shell).save(testTmpDir + "/t-28-toolbar-960x640.png")
        }
    }
}
