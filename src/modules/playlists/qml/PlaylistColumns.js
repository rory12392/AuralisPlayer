.pragma library

// 播放清單頁曲目清單的欄位目錄（畫面規格 S-08：同 S-07，多一欄拖曳把手；格式見核心的 TrackColumns.js）。
// 不 include 音樂庫模組的目錄，欄位與寬度照音樂庫清單的預設欄；寬度不夠時 TrackRow／ListHeader
// 依 priority 藏欄（介面規範 §4：曲名 > 藝人 > 長度 > 取樣率 > 專輯 > 格式）。
// 拖曳把手欄的格子是空的，把手本身由清單疊在這一欄的位置上（handleX）；它的 priority 和曲名一樣，
// 最後才藏。

function columns() {
    return [
        { id: "handle", title: "", width: 24, priority: 0, sortable: false, value: () => "" },
        { id: "title", title: qsTr("曲名"), weight: 3, sortable: false },
        { id: "artist", title: qsTr("藝人"), weight: 2, minWidth: 72, sortable: false },
        { id: "album", title: qsTr("專輯"), weight: 2, minWidth: 96, sortable: false },
        { id: "duration", title: qsTr("長度"), width: 64, align: "right", sortable: false },
        { id: "format", title: qsTr("格式"), width: 64, sortable: false },
        { id: "sampleRate", title: qsTr("取樣率／位元"), width: 136, sortable: false }
    ]
}
