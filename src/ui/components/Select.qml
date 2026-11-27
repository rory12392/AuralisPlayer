pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 下拉選擇（介面規範 §3，例：排序、取樣率、介面縮放）。label 顯示在值的左邊（inkMuted），同時是 Accessible.name。
// model：字串陣列，或和 SegmentedControl 同格式的 [{ text, enabled（預設 true）, reason（停用原因） }]；
// 停用的選項用 inkFaint、文字後面接「（<原因>）」，上下鍵、Home、End、打字搜尋與點擊都跳過它（目前值本身停用時照樣顯示為選取）。
// 鍵盤：Tab 停在這裡，上下鍵直接換值，空白鍵或 Enter 開清單，Esc 關閉。選了之後發出 activated(index)。
T.ComboBox {
    id: control

    property string label

    // model 讀回來是 QVariantList 的序列（不是 JS 的 Array），所以看 length 不看 Array.isArray
    function optionAt(i) {
        const list = control.model
        const option = list && typeof list === "object" && list.length !== undefined ? list[i] : undefined
        return option !== undefined && option !== null && typeof option === "object" ? option : null
    }

    function optionEnabled(i) {
        const option = optionAt(i)
        return option === null || option.enabled !== false
    }

    function optionReason(i) {
        const option = optionAt(i)
        return option !== null ? option.reason ?? "" : ""
    }

    // 從目前位置（清單開著時是醒目的那一列）往 step 方向找下一個可用的選項，找不到時回傳 -1
    function nextEnabled(step) {
        const from = popup.visible ? highlightedIndex : currentIndex
        for (let i = from + step; i >= 0 && i < count; i += step) {
            if (optionEnabled(i))
                return i
        }
        return -1
    }

    // 從頭（step 1）或從尾（step -1）找第一個可用的選項，找不到時回傳 -1
    function edgeEnabled(step) {
        for (let i = step > 0 ? 0 : count - 1; i >= 0 && i < count; i += step) {
            if (optionEnabled(i))
                return i
        }
        return -1
    }

    // 打字搜尋：目前位置之後（繞回開頭）第一個開頭是 text 的可用選項，找不到時回傳 -1
    function searchEnabled(text) {
        const key = text.toLowerCase()
        const from = popup.visible ? highlightedIndex : currentIndex
        for (let n = 1; n <= count; ++n) {
            const i = (from + n + count) % count
            if (optionEnabled(i) && textAt(i).toLowerCase().startsWith(key))
                return i
        }
        return -1
    }

    // 選到 target（可用的選項）：清單關著時直接換值並發出 activated；開著時只移動醒目的那一列
    function pick(target) {
        if (target < 0)
            return
        if (!popup.visible) {
            if (target !== currentIndex) {
                currentIndex = target
                activated(target)
            }
            return
        }
        while (highlightedIndex !== target) {
            if (highlightedIndex < target)
                incrementCurrentIndex()
            else
                decrementCurrentIndex()
        }
    }

    // 上下鍵跳過停用的選項
    function step(direction) {
        pick(nextEnabled(direction))
    }

    function isConfirmKey(key) {
        return key === Qt.Key_Return || key === Qt.Key_Enter || key === Qt.Key_Space
    }

    textRole: optionAt(0) !== null ? "text" : ""
    implicitWidth: Math.max(Theme.space24 + Theme.space12, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space3
    rightPadding: Theme.space2 + 16 + Theme.space2
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: label

    Keys.onUpPressed: event => control.step(-1)
    Keys.onDownPressed: event => control.step(1)
    // Home、End、打字搜尋都跳過停用的選項（元件內建的版本會選到停用的）；
    // 清單開著、醒目的是停用的選項時（例：用滑鼠移過去），Enter 與空白鍵按下、放開都不選它
    Keys.onPressed: event => {
        if (isConfirmKey(event.key) && popup.visible && !optionEnabled(highlightedIndex)) {
            event.accepted = true
        } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            pick(edgeEnabled(event.key === Qt.Key_Home ? 1 : -1))
            event.accepted = true
        } else if (event.text.length === 1 && event.text > " " &&
                   !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
            pick(searchEnabled(event.text))
            event.accepted = true
        }
    }
    Keys.onReleased: event => {
        if (isConfirmKey(event.key) && popup.visible && !optionEnabled(highlightedIndex))
            event.accepted = true
    }

    delegate: T.ItemDelegate {
        id: item
        required property int index
        readonly property string reason: control.optionReason(index)
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Theme.rowHeightCompact
        leftPadding: Theme.space3
        rightPadding: Theme.space3
        enabled: control.optionEnabled(index)
        text: reason.length > 0 ? qsTr("%1（%2）").arg(control.textAt(index)).arg(reason) : control.textAt(index)
        highlighted: control.highlightedIndex === index
        Accessible.name: text

        contentItem: Text {
            text: item.text
            font: control.currentIndex === item.index ? Theme.bodyMedium : Theme.body
            color: item.enabled ? Theme.ink : Theme.inkFaint
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: item.highlighted ? Theme.surfaceRaised : Theme.transparent
            radius: Theme.radius0
        }
    }

    // 值的寬度跟著內容區：Select 被限制寬度時值在箭頭前截斷，不壓到箭頭
    contentItem: Item {
        implicitWidth: valueText.x + valueText.implicitWidth
        implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)
        Text {
            id: labelText
            visible: control.label.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: control.label
            font: Theme.small
            color: Theme.inkMuted
        }
        Text {
            id: valueText
            objectName: "selectValue"
            x: labelText.visible ? labelText.implicitWidth + Theme.space2 : 0
            width: Math.max(0, parent.width - x)
            anchors.verticalCenter: parent.verticalCenter
            text: control.displayText
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
            elide: Text.ElideRight
        }
    }

    indicator: Icon {
        x: control.width - width - Theme.space2
        y: (control.height - height) / 2
        name: control.popup.visible ? "chevron-up" : "chevron-down"
        size: 16
        tone: control.enabled ? "ink" : "faint"
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.down || control.hovered || control.popup.visible ? Theme.surfaceRaised : Theme.transparent
        border.width: Theme.hairline
        border.color: !control.enabled ? Theme.line : control.popup.visible ? Theme.ink : Theme.lineStrong

        FocusFrame {
            visible: control.visualFocus
        }
    }

    popup: T.Popup {
        y: control.height + Theme.space1
        width: Math.max(control.width, Theme.sidebarWidth)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                                 Theme.rowHeightCompact * 10)
        padding: Theme.hairline

        contentItem: ListView {
            id: options
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll { flickable: options; lineHeight: Theme.rowHeightCompact }
        }

        background: Rectangle {
            color: Theme.surface
            border.width: Theme.hairline
            border.color: Theme.ink
            radius: Theme.radius0
        }
    }
}
