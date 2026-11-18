import QtQuick
import QtQuick.Templates as T

// 說明提示（介面規範 §3）：圖示按鈕的說明、被截斷的文字。surface 底、1px ink 框，沒有陰影。
// 用法：放在要說明的元件裡，visible 綁 hovered 或 visualFocus（共用元件 IconButton 已內建）。
T.ToolTip {
    id: control

    // Templates 的 Popup 沒有隱含尺寸，要自己由內容算（否則背景是 0 寬）
    implicitWidth: Math.min(contentItem.implicitWidth, Theme.panelWidth) + leftPadding + rightPadding
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - Theme.space1
    // hover 稍等一下才出現；鍵盤焦點時由使用的元件直接設 visible
    delay: Theme.motionTint
    margins: Theme.space2
    leftPadding: Theme.space2
    rightPadding: Theme.space2
    topPadding: Theme.space1
    bottomPadding: Theme.space1
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: Theme.small
        color: Theme.ink
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink
        radius: Theme.radius0
    }
}
