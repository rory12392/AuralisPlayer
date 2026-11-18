import QtQuick

// 1px 線性進度（介面規範 §3，例：背景工作、掃描）：lineStrong 軌道上的 ink 進度。
// indeterminate 時是一段來回移動的線；系統關掉動畫時停在中間不動。label 是 Accessible.name。
Item {
    id: line

    property real value: 0
    property bool indeterminate: false
    property string label

    readonly property real clamped: Math.max(0, Math.min(1, value))

    implicitWidth: Theme.sidebarWidth
    implicitHeight: Theme.hairline
    clip: true
    Accessible.role: Accessible.ProgressBar
    Accessible.name: label
    Accessible.description: indeterminate ? "" : qsTr("%1%").arg(Math.floor(clamped * 100))

    Rectangle {
        anchors.fill: parent
        color: Theme.lineStrong
    }

    Rectangle {
        id: bar
        height: parent.height
        width: line.indeterminate ? parent.width / 4 : line.clamped * parent.width
        x: line.indeterminate ? (parent.width - width) / 2 : 0
        color: Theme.ink

        SequentialAnimation on x {
            running: line.indeterminate && line.visible && Theme.motionTint > 0
            loops: Animation.Infinite
            NumberAnimation {
                from: -bar.width
                to: line.width
                duration: Theme.motionTint * 2
                easing.type: Easing.InOutQuad
            }
        }
    }
}
