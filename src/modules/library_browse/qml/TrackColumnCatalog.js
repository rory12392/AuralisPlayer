.pragma library

// 曲目清單（所有曲目、資料夾頁共用）的欄位目錄與「欄位」選單的增減（T-90，畫面規格 S-07）。
// 欄位格式同核心的 TrackColumns.js（T-89）；保存與還原用 TrackColumns.save()／restore()，存 LIB/trackColumns。
// 目錄的第一項 cover 不是文字欄：它代表 TrackRow／ListHeader 的 showCover，只在保存的清單與選單裡出現，
// 交給表頭與列之前要拿掉（columnsOf）。
// 寬度不夠時依 priority 藏欄（核心預設：曲名 0、藝人 1、長度 2、取樣率 3、專輯 4、格式 5、其他 10）；
// 藝人與專輯的 minWidth 讓藏欄的時機接近以前自己寫的 < 760／< 520 過濾（介面規範 §4）。
// 位元率、加入時間：音樂庫（LibraryQuery 的 TrackInfo）沒有這兩項資料，等核心提供後再加進目錄。

function numberText(value) {
    return value > 0 ? String(value) : ""
}

function catalog() {
    return [
        { id: "cover", title: qsTr("封面縮圖"), width: 32 },
        { id: "title", title: qsTr("曲名"), weight: 3, sortable: false },
        { id: "artist", title: qsTr("藝人"), weight: 2, minWidth: 72, sortable: false },
        { id: "album", title: qsTr("專輯"), weight: 2, minWidth: 96, sortable: false },
        { id: "duration", title: qsTr("長度"), width: 64, align: "right", sortable: false },
        { id: "format", title: qsTr("格式"), width: 64, sortable: false },
        { id: "sampleRate", title: qsTr("取樣率／位元"), width: 136, sortable: false },
        { id: "albumArtist", title: qsTr("專輯藝人"), weight: 2, minWidth: 96, sortable: false, optional: true },
        { id: "composer", title: qsTr("作曲者"), weight: 2, minWidth: 96, sortable: false, optional: true },
        { id: "year", title: qsTr("年份"), width: 56, align: "right", mono: true, sortable: false, optional: true,
          value: record => record ? numberText(record.year) : "" },
        { id: "trackNo", title: qsTr("曲序"), width: 48, align: "right", mono: true, sortable: false,
          optional: true, value: record => record ? numberText(record.trackNo) : "" },
        { id: "path", title: qsTr("路徑"), weight: 3, minWidth: 96, mono: true, sortable: false, optional: true }
    ]
}

// 欄位選單裡不能拿掉的欄（曲名吃剩下的寬度；restore 也會把它放回去）
function isLocked(id) {
    return id === "title"
}

// 保存的清單 → 交給 ListHeader／TrackRow 的欄位（拿掉 cover）
function columnsOf(layout) {
    return layout.filter(column => column.id !== "cover")
}

function hasCover(layout) {
    return layout.some(column => column.id === "cover")
}

function contains(layout, id) {
    return layout.some(column => column.id === id)
}

// 勾選或取消一欄：有就拿掉；沒有就照目錄的順序插回去（排在目錄裡在它後面、而且正在顯示的第一欄前面）。
// 曲名不能拿掉。回傳新的清單（保留其他欄的寬度）
function toggled(layout, entries, id) {
    if (contains(layout, id))
        return isLocked(id) ? layout : layout.filter(column => column.id !== id)
    const order = entries.findIndex(column => column.id === id)
    if (order < 0)
        return layout
    const before = layout.findIndex(column => {
        const index = entries.findIndex(entry => entry.id === column.id)
        return index > order
    })
    const result = layout.slice()
    result.splice(before < 0 ? result.length : before, 0, entries[order])
    return result
}
