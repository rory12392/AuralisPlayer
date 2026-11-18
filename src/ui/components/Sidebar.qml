pragma ComponentBehavior: Bound
import QtQuick

// 側欄（介面規範 §4）：品牌列、音樂庫的頁面、模組的側欄區塊（例：播放清單區）、設定。
Rectangle {
    id: sidebar

    width: Theme.sidebarWidth
    color: Theme.bg

    Rectangle {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: Theme.hairline
        color: Theme.line
    }

    // 品牌列：程式圖示的標記（介面規範 §7：ink 圓線加琥珀中心點，點的直徑是圓的 40%）＋字樣
    Item {
        id: brand
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.headerHeight

        Rectangle {
            id: mark
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.textHeading
            height: width
            radius: Theme.radiusFull
            color: Theme.transparent
            border.width: Theme.hairline
            border.color: Theme.ink
            Rectangle {
                anchors.centerIn: parent
                width: Theme.space2
                height: width
                radius: Theme.radiusFull
                color: Theme.amber
            }
        }
        Text {
            anchors.left: mark.right
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: "AURALIS"
            font: Theme.brand
            color: Theme.ink
        }
    }

    Flickable {
        id: pages
        anchors { left: parent.left; right: parent.right; top: brand.bottom; bottom: settingsItem.top }
        anchors.rightMargin: Theme.hairline
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll { flickable: pages; lineHeight: Theme.rowHeightCompact }

        Column {
            id: column
            width: parent.width

            Text {
                visible: UiRegistry.sidebarPages.length > 0
                leftPadding: Theme.space3
                topPadding: Theme.space4
                bottomPadding: Theme.space2
                text: qsTr("音樂庫")
                font: Theme.label
                color: Theme.inkMuted
            }
            Repeater {
                model: UiRegistry.sidebarPages
                delegate: SidebarItem {
                    id: pageItem
                    required property var modelData
                    width: column.width
                    text: pageItem.modelData.title
                    current: Navigator.currentPage === pageItem.modelData.id
                    onClicked: Navigator.open(pageItem.modelData.id)
                }
            }
            Repeater {
                model: UiRegistry.sidebarSections
                delegate: PageArea {
                    id: section
                    required property var modelData
                    width: column.width
                    height: section.item ? section.item.implicitHeight : Theme.rowHeight
                    source: section.modelData.source
                }
            }
        }
    }

    SidebarItem {
        id: settingsItem
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.rightMargin: Theme.hairline
        visible: UiRegistry.pageIds.indexOf("settings") >= 0
        height: visible ? Theme.rowHeightCompact : 0
        text: qsTr("設定")
        current: Navigator.currentPage === "settings"
        onClicked: Navigator.open("settings")
    }
}
