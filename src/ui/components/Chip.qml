import QtQuick
import QtQuick.Templates as T

// 篩選條件（介面規範 §3）。未套用：lineStrong 框、只顯示欄位名；已套用：ink 框、「欄位 值」＋叉號
// （畫面規格 S-03「格式 FLAC」）。
// 按下開 menu（共用元件 Menu）選值；叉號或 Delete／Backspace 發出 cleared()。
T.Button {
    id: control

    property string label
    property string value
    property Menu menu: null
    signal cleared()

    readonly property bool applied: value.length > 0

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Theme.chipHeight
    leftPadding: Theme.space3
    rightPadding: applied ? Theme.space1 : Theme.space3
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    text: applied ? qsTr("%1 %2").arg(label).arg(value) : label
    Accessible.name: text
    Accessible.description: applied ? qsTr("按 Delete 清除") : ""

    onClicked: {
        if (menu)
            menu.popup(control, 0, control.height + Theme.space1)
    }
    // Templates 的按鈕在 Windows 上只認空白鍵；介面規範要求 Enter 也能觸發
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onDeletePressed: if (applied) cleared()
    Keys.onBackPressed: if (applied) cleared()

    contentItem: Row {
        spacing: Theme.space1

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: Theme.small
            color: control.enabled ? Theme.ink : Theme.inkFaint
        }

        Item {
            visible: control.applied
            width: Theme.hitMin - Theme.space1
            height: control.availableHeight
            Icon {
                anchors.centerIn: parent
                name: "clear"
                size: 16
            }
            TapHandler {
                onTapped: control.cleared()
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.down || control.hovered ? Theme.surfaceRaised : Theme.transparent
        border.width: Theme.hairline
        border.color: !control.enabled ? Theme.line : control.applied ? Theme.ink : Theme.lineStrong

        FocusFrame {
            visible: control.visualFocus
        }
    }
}
