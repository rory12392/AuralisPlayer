import QtQuick
import QtQuick.Templates as T

// 進度與音量（介面規範 §3）：1px lineStrong 軌道；variant progress 的已播段是 3px amber（正在播放的訊號），
// volume 的已調段是 3px ink。把手是小方塊（只有直角與正圓，正圓保留給播放鍵等）。
// 左右鍵微調（stepSize 或 keyStep）；label 是 Accessible.name。
T.Slider {
    id: control

    property string label
    property string variant: "volume"
    // 只給左右鍵用的級距（0 表示用 stepSize）。設了 stepSize，滑鼠點擊與拖曳的值也會被取到級距上、
    // 把手不會停在滑鼠的位置；要落在滑鼠位置的（進度條）不設 stepSize，改設這個
    property real keyStep: 0

    Keys.onPressed: event => {
        if (keyStep <= 0 || (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right))
            return
        const forward = (event.key === Qt.Key_Right) !== mirrored
        const next = Math.max(from, Math.min(to, value + (forward ? keyStep : -keyStep)))
        event.accepted = true
        if (next !== value) {
            value = next
            moved()
        }
    }

    readonly property int thickness: 3 * Theme.hairline

    implicitWidth: Theme.sidebarWidth
    implicitHeight: Theme.hitMin
    padding: 0
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: label

    background: Item {
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Theme.hairline
            color: control.enabled ? Theme.lineStrong : Theme.line
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: control.visualPosition * parent.width
            height: control.thickness
            color: !control.enabled ? Theme.inkFaint
                   : control.variant === "progress" ? Theme.amber : Theme.ink
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        width: Theme.space2 + Theme.space1 / 2
        height: width
        radius: Theme.radius0
        visible: control.enabled
        color: control.pressed || control.hovered || control.visualFocus ? Theme.ink : Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink

        FocusFrame {
            visible: control.visualFocus
        }
    }
}
