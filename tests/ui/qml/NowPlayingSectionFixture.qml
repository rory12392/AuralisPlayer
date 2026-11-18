import QtQuick
import Auralis.Ui

// 正在播放面板的模組區塊（tst_now_playing_hooks，T-76）：像 LYR 的歌詞預覽三行，點一下切到歌詞頁籤。
Column {
    objectName: "fixtureNowPlayingSection"
    spacing: Theme.space1

    Repeater {
        model: ["上一行歌詞", "目前這一行", "下一行歌詞"]
        delegate: Text {
            required property string modelData
            required property int index
            text: modelData
            font: Theme.body
            color: index === 1 ? Theme.ink : Theme.inkMuted
        }
    }
    TapHandler {
        onTapped: Navigator.showPanel("panel/lyrics")
    }
}
