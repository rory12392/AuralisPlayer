import QtQuick
import Auralis.Ui

// 正在播放面板的歌詞預覽（畫面規格 S-11）：同步歌詞時三行，上一行、目前行（ink）、下一行（inkMuted）；
// 純文字歌詞與沒有歌詞時只佔一行小字說明，載入中留一行空白，切歌時高度不跳。
// 點一下切到歌詞頁籤（鍵盤用 Ctrl+L）。根是一般的 Item，寬度由面板決定、高度用 implicitHeight。
Item {
    id: preview

    objectName: "lyricsPreview"
    readonly property bool synced: Lyrics.state === "synced"
    readonly property int line: Lyrics.currentLine

    function lineAt(index) {
        return index >= 0 && index < Lyrics.lines.length ? Lyrics.lines[index] : ""
    }

    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("歌詞")

    Column {
        id: column
        width: preview.width
        topPadding: Theme.space3
        bottomPadding: Theme.space3
        spacing: Theme.space1

        Text {
            objectName: "lyricsPreviewPrevious"
            visible: preview.synced
            width: parent.width
            text: preview.lineAt(preview.line - 1)
            font: Theme.body
            color: Theme.inkMuted
            elide: Text.ElideRight
        }
        Text {
            objectName: "lyricsPreviewCurrent"
            width: parent.width
            text: preview.synced ? preview.lineAt(preview.line)
                : Lyrics.state === "plain" ? qsTr("這份歌詞沒有時間軸")
                : Lyrics.state === "instrumental" ? qsTr("純音樂")
                : Lyrics.state === "unavailable" ? qsTr("線上歌詞暫時無法取得")
                : Lyrics.state === "loading" ? "" : qsTr("沒有歌詞")
            font: Theme.body
            color: preview.synced ? Theme.ink : Theme.inkMuted
            elide: Text.ElideRight
        }
        Text {
            objectName: "lyricsPreviewNext"
            visible: preview.synced
            width: parent.width
            text: preview.lineAt(preview.line + 1)
            font: Theme.body
            color: Theme.inkMuted
            elide: Text.ElideRight
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: Navigator.showPanel("panel/lyrics")
    }
}
