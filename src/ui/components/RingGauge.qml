pragma ComponentBehavior: Bound
import QtQuick

// 正圓刻度儀表（介面規範 §3，例：首次掃描進度）：一圈 60 個刻度，已完成的刻度是 ink，其餘 lineStrong；
// 中間是等寬的百分比。value 是 0 到 1；label 是 Accessible.name。
Item {
    id: gauge

    property real value: 0
    property int size: Theme.space24 + Theme.space24
    property string label

    readonly property int ticks: 60
    readonly property int tickLength: Theme.space2
    readonly property real clamped: Math.max(0, Math.min(1, value))

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.ProgressBar
    Accessible.name: label
    Accessible.description: percent.text

    Repeater {
        model: gauge.ticks
        delegate: Item {
            id: tick
            required property int index
            width: gauge.width
            height: gauge.height
            rotation: index * 360 / gauge.ticks
            Rectangle {
                x: (tick.width - width) / 2
                y: 0
                width: Theme.hairline
                height: gauge.tickLength
                color: tick.index < Math.round(gauge.clamped * gauge.ticks) ? Theme.ink : Theme.lineStrong
            }
        }
    }

    Text {
        id: percent
        anchors.centerIn: parent
        text: qsTr("%1%").arg(Math.floor(gauge.clamped * 100))
        font: Theme.monoReadout
        color: Theme.ink
    }
}
