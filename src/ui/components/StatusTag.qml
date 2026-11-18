pragma ComponentBehavior: Bound
import QtQuick

// 狀態標籤（介面規範 §3）：6px 方形指示點＋文字（文字一定要有，不只靠顏色）。
// variant：neutral（ink 點）、amber（琥珀點＋光暈，例：位元完美）、signal（錯誤）、idle（空心點）。
// 光暈是全介面唯一的陰影：用幾層半透明的同心方框疊出由內往外變淡的效果，不經模糊效果；
// 只畫在點的周圍、不佔版面。
Row {
    id: tag

    property string text
    property string variant: "neutral"

    readonly property int dotSize: 6
    readonly property color dotColor: variant === "amber" ? Theme.amber
                                      : variant === "signal" ? Theme.signal
                                      : Theme.ink
    // 光暈的層數；每層往外擴 glowRadius / 2 / glowLayers。層數少時看得出一圈圈的方框，12 層才接近柔和的漸淡
    readonly property int glowLayers: 12

    spacing: Theme.space2
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Item {
        anchors.verticalCenter: parent.verticalCenter
        width: tag.dotSize
        height: tag.dotSize

        Repeater {
            model: tag.variant === "amber" ? tag.glowLayers : 0
            delegate: Rectangle {
                required property int index
                readonly property real spread: Theme.glowRadius / 2 * (index + 1) / tag.glowLayers
                anchors.centerIn: parent
                width: tag.dotSize + 2 * spread
                height: width
                radius: Theme.radius0
                color: Theme.glowAmber
                // 每層都疊在內層上，越往內疊越多層、越亮；中心疊滿時約等於 glowAmber 本身
                opacity: 1 / tag.glowLayers
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius0
            color: tag.variant === "idle" ? Theme.transparent : tag.dotColor
            border.width: tag.variant === "idle" ? Theme.hairline : 0
            border.color: Theme.inkMuted
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: tag.text
        font: Theme.label
        color: tag.variant === "amber" ? Theme.amber
               : tag.variant === "signal" ? Theme.signal
               : tag.variant === "idle" ? Theme.inkMuted
               : Theme.ink
    }
}
