pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import "TrackColumns.js" as TrackColumns

// 曲目列（介面規範 §3）：播放標記、封面縮圖、曲名、藝人、專輯、長度、格式、取樣率；欄位由頁面指定
// （columns，和 ListHeader 用同一份才會對齊；格式見 TrackColumns.js）。固定槽位以外的欄從 record 取值
// （例：record: model，欄 { id: "year", role: "year" }）。寬度不夠時依欄的 priority 藏欄（介面規範 §4）。狀態：hover、選取（highlighted，surfaceRaised 底）、
// 正在播放（6px amber 方點）、找不到檔案（文字轉 inkMuted，最右欄寫「找不到檔案」）。
// 清單的鍵盤操作（上下鍵、Enter 播放、選單鍵…）由頁面的 ListView 處理（介面規範 §7）。
T.ItemDelegate {
    id: row

    property string title
    property string artist
    property string album
    property string duration
    property string format
    property string sampleRate
    property url coverSource
    // 封面的 Accessible.name；空字串時用 album（借用 album 槽放別的值的清單要給，例：專輯清單放年份）
    property string coverName
    property bool showCover: true
    property bool playing: false
    property bool missing: false
    // 其他要標在列上的狀態（例：「無法播放」）；有值時取代最右欄
    property string statusText: missing ? qsTr("找不到檔案") : ""
    property var columns: TrackColumns.defaultColumns()
    // 列的資料（清單的 model 物件或 JS 物件）；固定槽位以外的欄依欄的 role（預設＝id）或 value 從這裡取值
    property var record: null

    readonly property var columnWidths: TrackColumns.widths(columns, cells.width, Theme.space3)
    // 最右邊顯示中的欄（statusText 取代它；窄視窗藏欄後不一定是清單的最後一欄）
    readonly property int lastShown: TrackColumns.lastShown(columnWidths)

    function valueOf(id) {
        switch (id) {
        case "title": return title
        case "artist": return artist
        case "album": return album
        case "duration": return duration
        case "format": return format
        case "sampleRate": return sampleRate
        }
        return ""
    }

    // 一欄要顯示的文字：value 函式 > 固定槽位（沒寫 role 時）> record[role ?? id]
    function textOf(column) {
        let value
        if (typeof column.value === "function")
            value = column.value(record)
        else if (column.role === undefined && TrackColumns.isFixedSlot(column.id))
            return valueOf(column.id)
        else
            value = record ? record[column.role ?? column.id] : undefined
        return value === undefined || value === null ? "" : String(value)
    }

    implicitWidth: Theme.panelWidth * 2
    implicitHeight: showCover ? Theme.rowHeight : Theme.rowHeightCompact
    leftPadding: Theme.space6
    rightPadding: Theme.space6
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: [title, artist, duration, statusText, playing ? qsTr("正在播放") : ""]
                     .filter(part => part.length > 0).join(" ")

    contentItem: Item {
        Item {
            id: mark
            width: TrackColumns.markWidth
            height: parent.height
            Rectangle {
                visible: row.playing
                anchors.centerIn: parent
                width: 6
                height: 6
                color: Theme.amber
            }
        }

        Cover {
            id: thumb
            objectName: "trackRowCover"
            visible: row.showCover
            anchors.left: mark.right
            anchors.verticalCenter: parent.verticalCenter
            size: Theme.coverThumb
            source: row.coverSource
            albumName: row.coverName.length > 0 ? row.coverName : row.album
        }

        Row {
            id: cells
            anchors.left: row.showCover ? thumb.right : mark.right
            anchors.leftMargin: Theme.space3
            anchors.right: parent.right
            height: parent.height
            spacing: Theme.space3

            Repeater {
                model: row.columns
                delegate: Text {
                    id: cell
                    required property var modelData
                    required property int index
                    readonly property bool last: index === row.lastShown
                    readonly property bool numeric: TrackColumns.isMono(modelData)
                    objectName: "trackCell_" + modelData.id
                    width: row.columnWidths[index] ?? 0
                    visible: width > 0
                    height: cells.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: modelData.align === "right" ? Text.AlignRight : Text.AlignLeft
                    text: last && row.statusText.length > 0 ? row.statusText : row.textOf(modelData)
                    font: last && row.statusText.length > 0 ? Theme.small
                          : numeric ? Theme.monoSmall : Theme.body
                    color: row.missing || (modelData.id !== "title" && !(last && row.statusText.length > 0))
                           ? Theme.inkMuted : Theme.ink
                    elide: Text.ElideRight
                }
            }
        }
    }

    background: Rectangle {
        color: row.highlighted || row.hovered || row.down ? Theme.surfaceRaised : Theme.transparent
        radius: Theme.radius0

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.leftMargin: Theme.space6
            anchors.rightMargin: Theme.space6
            height: Theme.hairline
            color: Theme.line
        }

        FocusFrame {
            anchors.margins: Theme.focusWidth
            visible: row.visualFocus
        }
    }
}
