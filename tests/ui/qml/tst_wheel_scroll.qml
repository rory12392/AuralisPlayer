import QtQuick
import QtTest
import Auralis.Ui

// 共用滾輪捲動 WheelScroll（介面規範 §3，T-92）：一格滾輪捲「Windows 一次捲動的行數」× 列高，
// 觸控板的 pixelDelta 照實際距離；到頂、到底不接走事件；點擊照常傳到清單列。
Item {
    id: root
    width: 400
    height: 600

    property int clicked: -1

    ListView {
        id: list
        width: 300
        height: 440
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: 100
        delegate: Rectangle {
            id: row
            required property int index
            width: ListView.view.width
            height: Theme.rowHeight
            color: Theme.surface
            TapHandler {
                onTapped: root.clicked = row.index
            }
        }
        WheelScroll {
            id: wheel
            flickable: list
        }
    }

    TestCase {
        name: "WheelScroll"
        when: windowShown

        readonly property int lines: Application.styleHints.wheelScrollLines

        function init() {
            list.contentY = list.originY
        }

        function test_one_notch_scrolls_system_lines() {
            verify(lines > 0, "Windows 一次捲動的行數：" + lines)
            mouseWheel(list, 150, 200, 0, -120)
            // 只捲一次、不再滑行（內建的滾輪捲動是用速度滑行，會停在別的位置）
            wait(300)
            compare(list.contentY, list.originY + lines * Theme.rowHeight)
            mouseWheel(list, 150, 200, 0, 120)
            wait(300)
            compare(list.contentY, list.originY)
        }

        function test_pixel_delta_not_amplified() {
            const event = { angleDelta: Qt.point(0, -120), pixelDelta: Qt.point(0, -30), accepted: true }
            compare(wheel.scroll(event), 30)
            compare(list.contentY, list.originY + 30)
            verify(event.accepted)
        }

        function test_fractional_notch_from_precision_touchpad() {
            const event = { angleDelta: Qt.point(0, -40), pixelDelta: Qt.point(0, 0), accepted: true }
            fuzzyCompare(wheel.scroll(event), lines * Theme.rowHeight / 3, 0.01)
        }

        function test_bounds_pass_event_through() {
            const up = { angleDelta: Qt.point(0, 120), pixelDelta: Qt.point(0, 0), accepted: true }
            compare(wheel.scroll(up), 0)
            verify(!up.accepted)
            list.contentY = list.originY + list.contentHeight - list.height
            const down = { angleDelta: Qt.point(0, -120), pixelDelta: Qt.point(0, 0), accepted: true }
            compare(wheel.scroll(down), 0)
            verify(!down.accepted)
            // 橫向的滾輪不接走
            const side = { angleDelta: Qt.point(-120, 0), pixelDelta: Qt.point(0, 0), accepted: true }
            compare(wheel.scroll(side), 0)
            verify(!side.accepted)
        }

        function test_clicks_reach_rows() {
            root.clicked = -1
            mouseClick(list, 150, Theme.rowHeight * 2 + 5)
            compare(root.clicked, 2)
        }
    }
}
