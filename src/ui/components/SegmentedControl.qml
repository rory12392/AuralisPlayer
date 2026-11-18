pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 互斥的 2 到 4 個選項（介面規範 §3）：檢視方式、DSD 輸出、ReplayGain、主題。
// model：[{ text, enabled（預設 true）, reason（停用原因，寫在選項上） }]；選取的那格是 ink 實心。
// 鍵盤：Tab 只停在選取的那格，左右鍵切到下一個可用的選項（像單選鈕）。選了之後發出 activated(index)。
Item {
    id: control

    property string label
    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.controlHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: label

    function optionEnabled(i) {
        const option = model[i]
        return option !== undefined && option.enabled !== false
    }

    // 從 from 往 step 方向找下一個可用的選項，找不到時回傳 -1
    function nextEnabled(from, step) {
        for (let i = from + step; i >= 0 && i < model.length; i += step) {
            if (optionEnabled(i))
                return i
        }
        return -1
    }

    function activeFocusItemInside() {
        for (let i = 0; i < segments.count; ++i) {
            if (segments.itemAt(i).activeFocus)
                return true
        }
        return false
    }

    function choose(i) {
        if (i < 0 || !optionEnabled(i))
            return
        // 先移焦點再改選取：Qt 不允許把目前有焦點的那格移出 Tab 順序
        const segment = segments.itemAt(i)
        if (segment && activeFocusItemInside())
            segment.forceActiveFocus(Qt.TabFocusReason)
        currentIndex = i
        activated(i)
    }

    Row {
        id: row
        height: parent.height
        // 相鄰兩格共用同一條框線
        spacing: -Theme.hairline

        Repeater {
            id: segments
            model: control.model
            delegate: T.Button {
                id: segment

                required property var modelData
                required property int index
                readonly property bool selected: control.currentIndex === index
                readonly property string reason: modelData.reason ?? ""

                height: row.height
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                leftPadding: Theme.space4
                rightPadding: Theme.space4
                enabled: control.optionEnabled(index)
                checkable: false
                // 按鈕本身的 checked 也要跟著選取：無障礙一啟用（例：UI Automation 連上）時，
                // Templates 的按鈕會用自己的 checked 蓋掉 Accessible.checked，否則預設選中的那格
                // 在選取改變之前一直回報沒選
                checked: selected
                activeFocusOnTab: selected
                focusPolicy: Qt.StrongFocus
                text: reason.length > 0 ? qsTr("%1（%2）").arg(modelData.text).arg(reason) : modelData.text
                Accessible.role: Accessible.RadioButton
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: selected

                onClicked: control.choose(index)
                Keys.onLeftPressed: control.choose(control.nextEnabled(index, -1))
                Keys.onRightPressed: control.choose(control.nextEnabled(index, 1))

                contentItem: Text {
                    text: segment.text
                    font: Theme.body
                    color: !segment.enabled ? Theme.inkFaint : segment.selected ? Theme.bg : Theme.ink
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: Theme.radius0
                    color: segment.selected ? Theme.ink
                           : segment.hovered || segment.down ? Theme.surfaceRaised : Theme.transparent
                    border.width: Theme.hairline
                    border.color: segment.selected ? Theme.ink : Theme.lineStrong

                    Behavior on color {
                        ColorAnimation { duration: Theme.motionState; easing.type: Easing.InOutQuad }
                    }

                    FocusFrame {
                        visible: segment.visualFocus
                    }
                }
            }
        }
    }
}
