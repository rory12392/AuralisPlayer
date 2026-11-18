import QtQuick
import QtQuick.Templates as T

// 對話框（介面規範 §3）：modal、焦點鎖在內部、Esc 關閉、關閉後焦點回到觸發元件。
// variant：default、confirm（破壞性確認：確認鈕用 signal，預設焦點在「取消」）。
// cancelText 是左邊（預設焦點）那顆的文字；confirmVisible 為 false 時只有那一顆（例：掃描報告的「關閉」）。
T.Dialog {
    id: dialog

    property string variant: "default"
    property string message: ""
    property string confirmText: ""
    property string cancelText: qsTr("取消")
    property bool confirmVisible: true
    property bool destructive: variant === "confirm"

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - 2 * Theme.space12 : Theme.panelWidth,
                    2 * Theme.panelWidth)
    padding: Theme.space6
    // Templates 的 Dialog 沒有隱含高度（沒設時高度是 0：沒有底色、頁尾疊到標題上），由標題、內容、頁尾算
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    // 關閉後焦點回到觸發元件由 Popup 本身處理（tests/ui 驗證過）
    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.hairline
        border.color: Theme.ink
        radius: Theme.radius0
    }

    // 不壓暗背景（介面規範：沒有陰影與遮罩，靠 1px ink 邊框分層），只擋住滑鼠
    T.Overlay.modal: Item {}

    header: Text {
        leftPadding: Theme.space6
        rightPadding: Theme.space6
        topPadding: Theme.space6
        text: dialog.title
        font: Theme.heading
        color: Theme.ink
        wrapMode: Text.Wrap
    }

    contentItem: Text {
        text: dialog.message
        font: Theme.body
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.space6
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: Theme.space6
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2
            Button {
                id: cancelButton
                text: dialog.cancelText
                onClicked: dialog.reject()
            }
            Button {
                visible: dialog.confirmVisible
                text: dialog.confirmText
                variant: dialog.destructive ? "signal" : "solid"
                onClicked: dialog.accept()
            }
        }
    }
}
