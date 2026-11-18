pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁「基礎」：色彩 token、字型 token（中英日混排，用來看字型退回順序）、圖示（介面規範 §6.2）。
Column {
    id: section

    readonly property string sample: "AuralisPlayer 96 kHz / 24 bit — 遺されたもの・夜に駆ける ＮieR：Automata C:\\音楽\\テスト"
    readonly property var colorTokens: [
        { name: "bg", value: Theme.bg }, { name: "surface", value: Theme.surface },
        { name: "surfaceRaised", value: Theme.surfaceRaised }, { name: "line", value: Theme.line },
        { name: "lineStrong", value: Theme.lineStrong }, { name: "ink", value: Theme.ink },
        { name: "inkMuted", value: Theme.inkMuted }, { name: "inkFaint", value: Theme.inkFaint },
        { name: "amber", value: Theme.amber }, { name: "signal", value: Theme.signal }
    ]
    readonly property var fontTokens: [
        { name: "title", font: Theme.title }, { name: "heading", font: Theme.heading },
        { name: "body", font: Theme.body }, { name: "bodyMedium", font: Theme.bodyMedium },
        { name: "small", font: Theme.small }, { name: "label", font: Theme.label },
        { name: "monoReadout", font: Theme.monoReadout }, { name: "monoSmall", font: Theme.monoSmall }
    ]

    spacing: Theme.space6

    Panel {
        width: section.width
        title: qsTr("色彩")
        Flow {
            width: parent.width
            spacing: Theme.space2
            Repeater {
                model: section.colorTokens
                delegate: Column {
                    id: swatch
                    required property var modelData
                    spacing: Theme.space1
                    Rectangle {
                        width: Theme.space24
                        height: Theme.space8
                        color: swatch.modelData.value
                        border.width: Theme.hairline
                        border.color: Theme.line
                    }
                    Text { text: swatch.modelData.name; font: Theme.monoSmall; color: Theme.inkMuted }
                }
            }
        }
    }

    Panel {
        width: section.width
        title: qsTr("字型（英數是 Jost／IBM Plex Mono，中日文退回系統字型）")
        Column {
            width: parent.width
            spacing: Theme.space2
            Repeater {
                model: section.fontTokens
                delegate: Row {
                    id: sampleRow
                    required property var modelData
                    width: parent.width
                    spacing: Theme.space4
                    Text {
                        width: Theme.space24
                        text: sampleRow.modelData.name
                        font: Theme.monoSmall
                        color: Theme.inkMuted
                    }
                    Text {
                        width: sampleRow.width - Theme.space24 - Theme.space4
                        text: section.sample
                        font: sampleRow.modelData.font
                        color: Theme.ink
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Panel {
        width: section.width
        title: "Icon"
        index: qsTr("%1 個").arg(iconNames.count)
        Flow {
            width: parent.width
            spacing: Theme.space4
            Repeater {
                id: iconNames
                model: IconLibrary.names()
                delegate: Column {
                    id: iconCell
                    required property string modelData
                    width: Theme.space24 + Theme.space6
                    spacing: Theme.space1
                    Row {
                        spacing: Theme.space3
                        Icon { name: iconCell.modelData; size: 16 }
                        Icon { name: iconCell.modelData; size: 20 }
                        Icon { name: iconCell.modelData; size: 20; tone: "muted" }
                    }
                    Text { text: iconCell.modelData; font: Theme.monoSmall; color: Theme.inkMuted }
                }
            }
        }
    }
}
