import QtQuick

// 讀數（介面規範 §3）：標籤＋等寬數值＋單位，例：「送到 DAC」96 kHz。tone：ink（預設）、amber。無互動。
Column {
    id: readout

    property string label
    property string value
    property string unit
    property string tone: "ink"

    spacing: Theme.space1
    Accessible.role: Accessible.StaticText
    Accessible.name: unit.length > 0 ? label + " " + value + " " + unit : label + " " + value

    Text {
        text: readout.label
        font: Theme.label
        color: Theme.inkMuted
    }
    Row {
        spacing: Theme.space1
        Text {
            id: valueText
            text: readout.value
            font: Theme.monoReadout
            color: readout.tone === "amber" ? Theme.amber : Theme.ink
        }
        Text {
            visible: readout.unit.length > 0
            anchors.baseline: valueText.baseline
            text: readout.unit
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
    }
}
