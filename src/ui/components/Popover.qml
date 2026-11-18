import QtQuick
import QtQuick.Templates as T

// 浮在觸發元件旁的浮層（例：輸出面板）：不是 modal，點外面或 Esc 關閉，關閉後焦點回到原本的元件
// （Popup 本身處理）。按觸發元件本身不算點外面，由觸發元件自己開關。
// surface 底、1px ink 框，沒有陰影與遮罩（和 Dialog、Menu 同一套分層）。內容設成 contentItem
// （由它的隱含尺寸決定浮層大小）。
T.Popup {
    id: control

    modal: false
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
    // Templates 的 Popup 沒有隱含尺寸，要自己由內容算
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink
        radius: Theme.radius0
    }
}
