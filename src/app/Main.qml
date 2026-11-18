import QtQuick
import Auralis.Ui

// 主視窗：大小、位置與面板開合存在 settings.ini 的 ui/ 段（ShellState）。
Window {
    id: window

    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight
    color: Theme.bg
    // 播放中「<曲名> - <藝人> — AuralisPlayer」（畫面規格 S-02），沒有藝人時省略「 - <藝人>」
    title: !Player.playing ? qsTr("AuralisPlayer")
           : Player.artist.length > 0 ? qsTr("%1 - %2 — AuralisPlayer").arg(Player.title).arg(Player.artist)
           : qsTr("%1 — AuralisPlayer").arg(Player.title)
    // 大小、位置、最大化與全螢幕：啟動時套用一次，之後不用繫結覆寫使用者的視窗狀態（T-92）
    WindowStateKeeper {
        id: windowState
        target: window
        fullscreen: Navigator.fullscreen
    }

    Component.onCompleted: windowState.restore()

    onClosing: windowState.save()

    // 從檔案總管拖進來的音樂檔或資料夾（AC-11-5）：放在殼的下面，殼裡接收曲目的地方不接的才輪到它
    FileDropArea {
        id: fileDrop
        anchors.fill: parent
    }

    AppShell {
        anchors.fill: parent
        // 空白鍵等沒被焦點元件用掉的按鍵要傳得到殼
        focus: true
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    FileDropHint {
        anchors.fill: parent
        visible: fileDrop.containsDrag
    }
}
