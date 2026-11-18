import QtQuick
import QtQuick.Templates as T
import Auralis.Ui

// 全螢幕頁（tst_now_playing_hooks，T-76）：像 LYR 的全螢幕正在播放，自己處理空白鍵與左右鍵；
// holdEscape 為 true 時（例：頁面裡開著浮層）自己用掉 Esc，殼不能因此離開全螢幕。
// 放幾個共用元件（tst_fullscreen_theme，T-94）：Paper 主題下也要用 Void 的 token。
Rectangle {
    id: page
    objectName: "fixtureFullscreenPage"
    color: Theme.bg
    focus: true

    property int spaces: 0
    property int arrows: 0
    property bool holdEscape: false

    Keys.onSpacePressed: page.spaces += 1
    Keys.onLeftPressed: page.arrows += 1
    Keys.onRightPressed: page.arrows += 1
    Keys.onEscapePressed: event => {
        if (page.holdEscape)
            page.holdEscape = false
        else
            event.accepted = false
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space6

        Slider {
            objectName: "fixtureFullscreenSlider"
            variant: "progress"
            value: 0.4
            label: "進度"
            focusPolicy: Qt.NoFocus
        }
        StatusTag {
            objectName: "fixtureFullscreenTag"
            variant: "amber"
            text: "位元完美"
        }
        ScrollBar {
            objectName: "fixtureFullscreenScrollBar"
            orientation: Qt.Horizontal
            policy: T.ScrollBar.AlwaysOn
            size: 0.3
        }
    }
}
