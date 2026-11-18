pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 面板頁籤（介面規範 §3，例：正在播放／歌詞／佇列）：底線式，目前頁籤是 ink 字＋2px ink 底線。
// model：頁籤文字的陣列。Tab 只停在目前的頁籤，左右鍵切換；切換時 currentIndex 改變。
T.TabBar {
    id: control

    property var model: []

    implicitWidth: contentWidth
    implicitHeight: Theme.controlHeight + Theme.space2
    spacing: Theme.space4

    function select(i) {
        if (i < 0 || i >= count)
            return
        currentIndex = i
        itemAt(i).forceActiveFocus(Qt.TabFocusReason)
    }

    Repeater {
        model: control.model
        delegate: T.TabButton {
            id: tab
            required property var modelData
            required property int index

            // 寫明寬度，不讓 TabBar 把每個頁籤平均分配（文字長短不同時會重疊）
            width: implicitContentWidth
            implicitWidth: implicitContentWidth
            implicitHeight: control.implicitHeight
            padding: 0
            text: modelData
            activeFocusOnTab: tab.checked
            focusPolicy: Qt.StrongFocus
            Accessible.name: text
            Accessible.role: Accessible.PageTab

            Keys.onLeftPressed: control.select(index - 1)
            Keys.onRightPressed: control.select(index + 1)

            contentItem: Text {
                text: tab.text
                font: tab.checked ? Theme.bodyMedium : Theme.body
                color: !tab.enabled ? Theme.inkFaint : tab.checked || tab.hovered ? Theme.ink : Theme.inkMuted
                verticalAlignment: Text.AlignVCenter
            }

            background: Item {
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 2 * Theme.hairline
                    color: tab.checked ? Theme.ink : Theme.transparent
                }
                FocusFrame {
                    visible: tab.visualFocus
                }
            }
        }
    }

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
    }

    background: Item {
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.hairline
            color: Theme.line
        }
    }
}
