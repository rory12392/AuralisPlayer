import QtQuick

// 鍵盤焦點外框（介面規範 §3）：2px focus 色實線，與元件之間留 2px。只在 visualFocus 時顯示。
Rectangle {
    anchors.fill: parent
    anchors.margins: -2 * Theme.focusWidth
    color: Theme.transparent
    border.width: Theme.focusWidth
    border.color: Theme.focus
    radius: Theme.radius0
}
