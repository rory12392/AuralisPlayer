pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib
import "ListActions.js" as ListActions

// 專輯詳細（畫面規格 S-04，US-10 AC-4）：開頁參數 {albumId}（專輯頁、藝人頁雙擊或 Enter）。
// 頂部：封面 200、專輯名、專輯藝人、等寬的「<年份>　<n> 首　<總長>　<格式摘要>」、「播放專輯」「加到佇列最後」；
// 頁面工具列左側是「返回」（Alt+← 由主視窗處理，同 Navigator.back）。下方曲目清單：曲序、曲名、
// 藝人（與專輯藝人不同時才顯示）、長度、格式、取樣率／位元；多片時依碟分組（「碟 <n>」）。
// CUE 曲目與一般曲目同一種列（AC-07-1）。雙擊或 Enter＝整張專輯從那一首開始播（AC-10-4）。
// 資料是 LibraryAlbum（用 LibraryAlbums 讀好的專輯）。狀態：載入、有資料、錯誤（讀取失敗、
// 專輯已不在音樂庫）。T-25：曲目清單的選取（Shift＋上下鍵、Ctrl＋點擊、Ctrl+A）、Shift+Enter 接著播放、
// Ctrl+Enter 加到佇列最後、右鍵選單、拖放。
Item {
    id: page
    objectName: "libraryAlbumPage"

    readonly property bool failed: LibraryAlbum.error.length > 0 && !LibraryAlbum.loaded
    readonly property bool loading: !failed && !LibraryAlbum.loaded
    // 重新掃描後專輯被移除，或參數不對
    readonly property bool missing: LibraryAlbum.loaded && !LibraryAlbum.found
    readonly property bool ready: LibraryAlbum.loaded && LibraryAlbum.found
    // 主視窗的狀態列（PageArea）
    property string statusText: ready
        ? qsTr("%1 首").arg(Number(LibraryAlbum.trackCount).toLocaleString(Qt.locale(), "f", 0)) : ""
    property string statusTextLeft: ready ? ListActions.statusText(selection.trackCount) : ""

    // 曲目清單的選取、右鍵選單與鍵盤（T-25）
    TrackSelection {
        id: selection
        model: LibraryAlbum.tracks
        allQuery: selection.albumQuery(LibraryAlbum.albumId)
    }

    TrackMenu {
        id: menu
    }

    readonly property var actions: ({
        list: list,
        selection: selection,
        menu: menu,
        player: Player,
        contain: ListView.Contain,
        rowStep: 1,
        colStep: 0,
        activate: row => LibraryAlbum.play(row)
    })

    // S-04 的大封面（沒有對應的 token，見模組規格整合待辦）
    readonly property int coverSize: 200

    // 開頁參數；離開這一頁時 Navigator.params 會先換成下一頁的，所以只在這一頁是目前頁時跟著換
    readonly property var albumId: Navigator.currentPage === "library/album" ? Navigator.params.albumId : undefined
    onAlbumIdChanged: {
        if (albumId !== undefined)
            LibraryAlbum.open(Number(albumId))
    }
    Component.onCompleted: {
        if (albumId !== undefined)
            LibraryAlbum.open(Number(albumId))
    }

    // 頁面工具列（AppShell 放在搜尋框右邊）：返回
    property Component toolbar: Row {
        objectName: "albumToolbar"
        Button {
            objectName: "albumBack"
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("返回")
            enabled: Navigator.canGoBack
            onClicked: Navigator.back()
        }
    }

    // 介面規範 §4：960 寬（頁面寬 < 760）拿掉「格式」欄。曲目照碟號、軌號排，表頭不能排序
    // （sortable: false，表頭不進 Tab 順序：播放專輯 → 加到佇列最後 → 曲目清單）
    readonly property bool compact: width < 760
    readonly property var columns: {
        const all = [
            { id: "album", title: qsTr("曲序"), width: 32, align: "right", sortable: false },
            { id: "title", title: qsTr("曲名"), weight: 3, sortable: false },
            { id: "artist", title: qsTr("藝人"), weight: 2, sortable: false },
            { id: "duration", title: qsTr("長度"), width: 64, align: "right", sortable: false },
            { id: "format", title: qsTr("格式"), width: 64, sortable: false },
            { id: "sampleRate", title: qsTr("取樣率／位元"), width: 136, sortable: false }
        ]
        return all.filter(c => (c.id !== "artist" || LibraryAlbum.hasTrackArtists)
                               && (c.id !== "format" || !page.compact))
    }

    // ---- 頂部：封面與專輯資訊 ----
    Item {
        id: header
        objectName: "albumHeader"
        visible: page.ready
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: page.coverSize + 2 * Theme.space6

        Cover {
            id: cover
            objectName: "albumCover"
            x: Theme.space6
            y: Theme.space6
            size: page.coverSize
            source: LibraryAlbum.coverUrl
            albumName: LibraryAlbum.title
        }

        Column {
            anchors { left: cover.right; leftMargin: Theme.space6; right: parent.right
                      rightMargin: Theme.space6; bottom: cover.bottom }
            spacing: Theme.space2

            Text {
                objectName: "albumTitle"
                width: parent.width
                text: LibraryAlbum.title
                font: Theme.heading
                color: Theme.ink
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                objectName: "albumArtist"
                width: parent.width
                text: LibraryAlbum.artist
                font: Theme.body
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            Text {
                objectName: "albumSummary"
                width: parent.width
                text: LibraryAlbum.summaryText
                font: Theme.monoSmall
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            Item {
                width: 1
                height: Theme.space2
            }
            Row {
                spacing: Theme.space2
                Button {
                    objectName: "albumPlay"
                    variant: "solid"
                    text: qsTr("播放專輯")
                    enabled: LibraryAlbum.trackCount > 0
                    onClicked: LibraryAlbum.play(0)
                }
                Button {
                    objectName: "albumEnqueue"
                    text: qsTr("加到佇列最後")
                    enabled: LibraryAlbum.trackCount > 0
                    onClicked: LibraryAlbum.enqueueAll()
                }
            }
        }
    }

    // ---- 曲目清單 ----
    ListHeader {
        id: listHeader
        visible: page.ready
        anchors { left: parent.left; right: parent.right; top: header.bottom }
        showCover: false
        columns: page.columns
    }

    ListView {
        id: list
        objectName: "albumTrackList"
        visible: page.ready
        anchors { left: parent.left; right: parent.right; top: listHeader.bottom; bottom: parent.bottom }
        clip: true
        model: LibraryAlbum.tracks
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        reuseItems: true
        cacheBuffer: Theme.rowHeightCompact * 10
        Accessible.role: Accessible.List
        Accessible.name: qsTr("曲目")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: list; lineHeight: Theme.rowHeightCompact }

        Keys.onPressed: event => event.accepted = ListActions.handleKey(page.actions, event)

        // 多片時依碟分組；只有一片時不顯示組標題
        section.property: LibraryAlbum.discCount > 1 ? "discNo" : ""
        section.delegate: Item {
            id: discHeader
            required property string section
            width: ListView.view.width
            height: Theme.rowHeightCompact
            Accessible.role: Accessible.Heading
            Accessible.name: discLabel.text

            Text {
                id: discLabel
                anchors { left: parent.left; leftMargin: Theme.space6; verticalCenter: parent.verticalCenter }
                text: qsTr("碟 %1").arg(discHeader.section)
                font: Theme.label
                color: Theme.inkMuted
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.hairline
                color: Theme.line
            }
        }

        // 拖放來源包住列；內容是選取的曲目（同右鍵選單）
        delegate: TrackDragArea {
            id: drag
            required property int index
            required property var model

            width: ListView.view.width
            height: row.height
            count: selection.revision >= 0 && selection.isSelected(drag.index) ? selection.trackCount : 1
            query: drag.dragging ? selection.query() : ""
            tracks: drag.dragging && !selection.queryOnly ? selection.tracks() : []
            onDraggingChanged: {
                if (drag.dragging)
                    selection.ensureSelected(drag.index)
            }

            TrackRow {
                id: row
                width: parent.width
                showCover: false
                // 鍵盤焦點在清單本身（上下鍵、Enter 由清單處理），列不另外取得焦點
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: selection.revision >= 0 && selection.isSelected(drag.index)
                columns: page.columns
                album: drag.model.trackNo > 0 ? String(drag.model.trackNo) : ""
                title: drag.model.title
                // 專輯換掉的瞬間舊列的 model 可能已經沒有值
                artist: (drag.model.artist ?? "") !== LibraryAlbum.artist ? (drag.model.artist ?? "") : ""
                duration: drag.model.durationText
                format: drag.model.format
                sampleRate: drag.model.qualityText
                playing: LibraryPlayback.currentKey.length > 0
                         && LibraryPlayback.currentKey === LibraryPlayback.keyOf(drag.model.path, drag.model.subsong)
                onClicked: ListActions.click(page.actions, drag.index)
                onDoubleClicked: LibraryAlbum.play(drag.index)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: ListActions.openMenuAt(page.actions, drag.index)
                }
            }
        }
    }

    // ---- 載入中（超過 150 毫秒才出現，Skeleton 自己處理）----
    Item {
        objectName: "albumLoading"
        visible: page.loading
        anchors.fill: parent

        Skeleton {
            id: coverSkeleton
            x: Theme.space6
            y: Theme.space6
            variant: "cover"
            size: page.coverSize
        }
        Column {
            anchors { left: parent.left; right: parent.right; top: coverSkeleton.bottom; topMargin: Theme.space6 }
            Repeater {
                model: 6
                delegate: Skeleton {
                    width: parent ? parent.width : 0
                    variant: "row"
                }
            }
        }
    }

    ErrorBlock {
        objectName: "albumError"
        visible: page.failed || page.missing
        anchors.fill: parent
        message: page.missing ? qsTr("找不到這張專輯，可能在重新掃描後已經從音樂庫移除。")
                              : qsTr("無法載入專輯。")
        onRetry: LibraryAlbum.reload()
    }
}
