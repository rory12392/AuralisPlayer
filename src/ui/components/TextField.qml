import QtQuick
import QtQuick.Templates as T

// 單行輸入（介面規範 §3）：上方標籤、只有底線；焦點時底線轉 amber，錯誤時底線與下方說明用 signal。
// label 必填（同時是 Accessible.name），errorText 非空時是錯誤狀態並設進 Accessible.description。
Item {
    id: control

    property string label
    property string errorText
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias input: input
    signal accepted()

    readonly property bool hasError: errorText.length > 0

    implicitWidth: Theme.panelWidth
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: Theme.space1

        Text {
            text: control.label
            font: Theme.label
            color: control.enabled ? Theme.inkMuted : Theme.inkFaint
        }

        T.TextField {
            id: input
            width: parent.width
            implicitHeight: Theme.controlHeight
            leftPadding: 0
            rightPadding: 0
            font: Theme.body
            color: control.enabled ? Theme.ink : Theme.inkFaint
            placeholderTextColor: Theme.inkMuted
            selectionColor: Theme.ink
            selectedTextColor: Theme.bg
            verticalAlignment: TextInput.AlignVCenter
            activeFocusOnTab: true
            Accessible.name: control.label
            Accessible.description: control.errorText
            onAccepted: control.accepted()

            background: Item {
                // Templates 的 TextField 不畫提示文字，由這裡畫
                Text {
                    visible: input.length === 0 && input.preeditText.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: input.placeholderText
                    font: Theme.body
                    color: control.enabled ? Theme.inkMuted : Theme.inkFaint
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: input.activeFocus || control.hasError ? 2 * Theme.hairline : Theme.hairline
                    color: control.hasError ? Theme.signal
                           : input.activeFocus ? Theme.amber
                           : control.enabled ? Theme.lineStrong : Theme.line
                }
            }
        }

        Text {
            visible: control.hasError
            width: parent.width
            text: control.errorText
            font: Theme.small
            color: Theme.signal
            wrapMode: Text.Wrap
        }
    }
}
