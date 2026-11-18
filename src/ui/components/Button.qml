import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T

// 文字按鈕（介面規範 §3）。variant：outline（預設，lineStrong 細框）、solid（ink 實心）、
// signal（破壞性操作）。busy：送出中，文字旁顯示轉動的 1px 圓弧並停用。
// 外部只設位置與大小，不覆寫顏色與字體。
T.Button {
    id: control

    property string variant: "outline"
    property bool busy: false

    // 忙碌時停用；結束後還原外部原本的 enabled 繫結
    Binding on enabled {
        when: control.busy
        value: false
    }
    Accessible.description: busy ? qsTr("處理中") : ""

    // Templates 的按鈕在 Windows 上只認空白鍵；介面規範要求 Enter 也能觸發
    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    readonly property color inkColor: !enabled ? Theme.inkFaint
                                     : variant === "solid" ? Theme.bg
                                     : variant === "signal" ? Theme.signal
                                     : Theme.ink

    implicitWidth: Math.max(Theme.hitMin, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space4
    rightPadding: Theme.space4
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text

    contentItem: Item {
        implicitWidth: label.implicitWidth + (spinner.visible ? spinner.width + Theme.space2 : 0)
        implicitHeight: label.implicitHeight

        Text {
            id: label
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: spinner.visible ? -(spinner.width + Theme.space2) / 2 : 0
            width: Math.min(implicitWidth, parent.width - (spinner.visible ? spinner.width + Theme.space2 : 0))
            text: control.text
            font: Theme.body
            color: control.inkColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 忙碌指示：四分之三圈的 1px 圓弧；系統關掉動畫時不轉
        Shape {
            id: spinner
            visible: control.busy
            anchors.left: label.right
            anchors.leftMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.space3
            height: Theme.space3
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: control.inkColor
                strokeWidth: Theme.hairline
                fillColor: Theme.transparent
                capStyle: ShapePath.FlatCap
                PathAngleArc {
                    centerX: spinner.width / 2
                    centerY: spinner.height / 2
                    radiusX: spinner.width / 2 - Theme.hairline
                    radiusY: radiusX
                    startAngle: 0
                    sweepAngle: 270
                }
            }

            RotationAnimator on rotation {
                running: control.busy && Theme.motionTint > 0
                from: 0
                to: 360
                loops: Animation.Infinite
                duration: Theme.motionTint * 2
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius0
        color: control.variant === "solid"
               ? (control.enabled ? Theme.ink : Theme.surfaceRaised)
               : (control.down || control.hovered || control.checked ? Theme.surfaceRaised
                                                                    : Theme.transparent)
        border.width: Theme.hairline
        border.color: !control.enabled ? Theme.line
                      : control.variant === "solid" ? Theme.ink
                      : control.variant === "signal" ? Theme.signal
                      : control.checked ? Theme.ink
                      : Theme.lineStrong

        Behavior on color {
            ColorAnimation { duration: Theme.motionState; easing.type: Easing.InOutQuad }
        }

        FocusFrame {
            visible: control.visualFocus
        }
    }
}
