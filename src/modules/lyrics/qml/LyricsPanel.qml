import QtQuick
import Auralis.Ui

// 歌詞面板（畫面規格 S-12，panel/lyrics）：同步歌詞的目前行高亮並保持在 40% 處（AC-15-2），
// 點一行跳到該時間（AC-15-3）；純文字歌詞不高亮、頂部小字說明；沒有歌詞時 EmptyState（AC-15-5）。
// 底部小字寫來源。純音樂（"instrumental"）與線上逾時（"unavailable"，AC-16-2：＋「重試」）
// 也用 EmptyState（T-67）。
Item {
    id: panel

    objectName: "lyricsPanel"
    readonly property bool hasLines: Lyrics.state === "synced" || Lyrics.state === "plain"

    Text {
        id: plainNote
        objectName: "lyricsPlainNote"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        visible: Lyrics.state === "plain"
        topPadding: Theme.space4
        text: qsTr("這份歌詞沒有時間軸")
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }

    LyricsList {
        id: list
        objectName: "lyricsList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: plainNote.visible ? plainNote.bottom : parent.top
        anchors.bottom: sourceNote.visible ? sourceNote.top : parent.bottom
        visible: panel.hasLines
        lines: Lyrics.lines
        activeLine: Lyrics.currentLine
        synced: Lyrics.state === "synced"
        onLineActivated: index => Lyrics.seekToLine(index)
    }

    Text {
        id: sourceNote
        objectName: "lyricsSource"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        visible: panel.hasLines && text.length > 0
        topPadding: Theme.space2
        bottomPadding: Theme.space3
        text: Lyrics.source === "lrc" ? qsTr("來源：同名 .lrc")
            : Lyrics.source === "embedded" ? qsTr("來源：檔案內嵌")
            : Lyrics.source === "lrclib" ? qsTr("來源：LRCLIB") : ""
        font: Theme.small
        color: Theme.inkMuted
        elide: Text.ElideRight
    }

    Column {
        objectName: "lyricsLoading"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.topMargin: Theme.space4
        visible: Lyrics.state === "loading"
        Repeater {
            model: 3
            Skeleton {
                width: parent.width
                variant: "row"
            }
        }
    }

    EmptyState {
        objectName: "lyricsEmpty"
        anchors.fill: parent
        anchors.margins: Theme.space6
        visible: Lyrics.state === "none"
        title: qsTr("沒有歌詞")
        description: Lyrics.onlineEnabled ? "" : qsTr("線上抓取已關閉，可在設定開啟。")
    }

    EmptyState {
        objectName: "lyricsInstrumental"
        anchors.fill: parent
        anchors.margins: Theme.space6
        visible: Lyrics.state === "instrumental"
        title: qsTr("純音樂")
    }

    EmptyState {
        objectName: "lyricsUnavailable"
        anchors.fill: parent
        anchors.margins: Theme.space6
        visible: Lyrics.state === "unavailable"
        title: qsTr("線上歌詞暫時無法取得")
        actionText: qsTr("重試")
        onAction: Lyrics.retry()
    }
}
