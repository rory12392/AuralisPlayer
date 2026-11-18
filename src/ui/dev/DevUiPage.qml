pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁（介面規範 §8，只在開發建置）：介面規範 §3 每個元件的變體與狀態，每個元件一個 Panel。
// 分成幾個區段（頁籤）；開頁參數 section 直接開到某一段（截圖用：--screenshot "dev/ui?section=lists"），
// open 參數在浮層區段直接打開選單或對話框（menu、dialog）。
Item {
    id: page

    readonly property var sections: [
        { id: "foundation", title: qsTr("基礎"), source: "DevFoundation.qml" },
        { id: "controls", title: qsTr("控制項"), source: "DevControls.qml" },
        { id: "display", title: qsTr("顯示與狀態"), source: "DevDisplay.qml" },
        { id: "lists", title: qsTr("清單與拖放"), source: "DevLists.qml" },
        { id: "overlays", title: qsTr("浮層"), source: "DevOverlays.qml" }
    ]
    readonly property string requested: Navigator.params.section ?? "controls"
    property string themeChoice: "system"

    // 狀態列顯示目前區段（頁面可以提供 statusText，介面規範 §4）
    readonly property string statusText: qsTr("元件展示：%1").arg(sections[tabs.currentIndex].title)

    Rectangle {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.headerHeight
        color: Theme.bg

        Tabs {
            id: tabs
            anchors.left: parent.left
            anchors.leftMargin: Theme.space6
            anchors.bottom: parent.bottom
            model: page.sections.map(section => section.title)
            currentIndex: Math.max(0, page.sections.findIndex(section => section.id === page.requested))
        }

        SegmentedControl {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space6
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("主題")
            model: [{ text: qsTr("跟隨系統") }, { text: qsTr("深色") }, { text: qsTr("淺色") }]
            currentIndex: ["system", "void", "paper"].indexOf(page.themeChoice)
            onActivated: index => {
                page.themeChoice = ["system", "void", "paper"][index]
                ThemeController.overrideMode(page.themeChoice)
            }
        }
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        contentHeight: loader.height + 2 * Theme.space6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: flick }

        Loader {
            id: loader
            x: Theme.space6
            y: Theme.space6
            width: flick.width - 2 * Theme.space6
            height: item ? (item as Item).implicitHeight : 0
            source: page.sections[tabs.currentIndex].source
        }
    }
}
