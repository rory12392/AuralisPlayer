import QtQuick
import QtQuick.Templates as T

// 只有圖示的按鈕（介面規範 §3）。text 必填：同時是 Accessible.name 與 ToolTip。
// size：24（圖示 16）或 40（圖示 20）；variant：plain（預設）、round（播放鍵，48 的 1px ink 正圓框）。
// checkable 時選取狀態是 surfaceRaised 底＋ink 框（例：正在播放面板開關、輸出按鈕）。
T.Button {
    id: control

    property string iconName
    property int size: 24
    property string variant: "plain"

    readonly property bool round: variant === "round"
    readonly property int side: round ? Theme.space12 : size

    implicitWidth: side
    implicitHeight: side
    padding: 0
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.description: checkable ? (checked ? qsTr("開啟") : qsTr("關閉")) : ""

    // Templates 的按鈕在 Windows 上只認空白鍵；介面規範要求 Enter 也能觸發
    Keys.onReturnPressed: pressByKeyboard()
    Keys.onEnterPressed: pressByKeyboard()

    function pressByKeyboard() {
        if (checkable) {
            toggle()
            toggled()
        }
        clicked()
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.round || control.size >= 40 ? 20 : 16
            tone: control.enabled ? "ink" : "faint"
        }
    }

    background: Rectangle {
        radius: control.round ? Theme.radiusFull : Theme.radius0
        color: control.down || control.hovered || control.checked ? Theme.surfaceRaised : Theme.transparent
        border.width: control.round || control.checked ? Theme.hairline : 0
        border.color: control.enabled ? Theme.ink : Theme.line

        Behavior on color {
            ColorAnimation { duration: Theme.motionState; easing.type: Easing.InOutQuad }
        }

        FocusFrame {
            visible: control.visualFocus
            radius: control.round ? Theme.radiusFull : Theme.radius0
        }
    }

    ToolTip {
        text: control.text
        visible: control.text.length > 0 && (control.hovered || control.visualFocus)
    }
}
