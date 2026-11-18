import QtQuick

// 從檔案總管拖進主視窗的音樂檔或資料夾（AC-11-5）：放開＝加到佇列最後並開始播放，不加入音樂庫。
// 主視窗把它放在殼的下面、蓋滿整個視窗（播放列、佇列面板都算）：殼裡接收曲目的 TrackDropArea
// 只認視窗內拖曳的曲目，檔案拖進來時它不接，才輪到這裡；反過來，視窗內拖曳的曲目這裡不接。
// 拖曳經過時的提示由 FileDropHint 畫（要疊在殼的上面）。
DropArea {
    id: area

    keys: ["text/uri-list"]

    onEntered: drag => {
        if (!drag.hasUrls)
            drag.accepted = false
    }
    onDropped: drop => {
        if (!drop.hasUrls) {
            drop.accepted = false
            return
        }
        drop.acceptProposedAction()
        OpenFiles.openUrls(drop.urls)
    }
}
