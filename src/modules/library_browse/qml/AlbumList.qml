pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib
import "ListActions.js" as ListActions

// 專輯的清單檢視（S-03）：一列一張專輯：封面縮圖、專輯、專輯藝人、年份、曲數、總長、格式。
// 列用共用的 TrackRow（欄位是曲目的固定槽位，這裡借用：album 槽放年份、sampleRate 槽放曲數）；
// 正在播放的專輯列前有琥珀方點。鍵盤焦點在清單本身（上下鍵、Enter 開專輯詳細）。
// T-25：以整張專輯為單位選取（Shift＋上下鍵、Ctrl＋點擊、Ctrl+A）、Shift+Enter／Ctrl+Enter、右鍵選單、拖放。
Item {
    id: view

    signal openRequested(int row)

    // 專輯的來源：LibraryAlbums 或 LibraryArtists（T-23），見 AlbumGrid.source
    property var source: LibraryAlbums
    readonly property alias selection: selection
    /// 狀態列左側的選取說明（頁面的 statusTextLeft）
    readonly property string selectionText: ListActions.statusText(selection.trackCount)

    TrackSelection {
        id: selection
        model: list.model
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
        activate: row => view.openRequested(row)
    })

    // 960 寬（頁面寬 < 760）時拿掉「年份」與「格式」欄（介面規範 §4 的精神：次要欄位先拿掉）
    readonly property bool compact: width < 760
    readonly property var columns: {
        const all = [
            { id: "title", title: qsTr("專輯"), weight: 3 },
            { id: "artist", title: qsTr("專輯藝人"), weight: 2 },
            { id: "album", title: qsTr("年份"), width: 56, align: "right" },
            { id: "sampleRate", title: qsTr("曲數"), width: 48, align: "right" },
            { id: "duration", title: qsTr("總長"), width: 72, align: "right" },
            { id: "format", title: qsTr("格式"), width: 72 }
        ]
        return compact ? all.filter(c => c.id !== "album" && c.id !== "format") : all
    }

    ListHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        columns: view.columns
    }

    ListView {
        id: list
        objectName: "albumList"
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        clip: true
        // 關閉程式時單例可能比頁面先被拆掉（source 變 null），繫結要防呆
        model: view.source ? view.source.albums : null
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        reuseItems: true
        cacheBuffer: Theme.rowHeight * 10
        currentIndex: -1
        Accessible.role: Accessible.List
        Accessible.name: qsTr("專輯")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: list }

        Keys.onPressed: event => event.accepted = ListActions.handleKey(view.actions, event)
        onActiveFocusChanged: {
            if (activeFocus && currentIndex < 0 && count > 0)
                currentIndex = 0
        }

        // 拖放來源包住列；內容是選取的整張專輯
        delegate: TrackDragArea {
            id: drag
            required property int index
            required property var model

            width: ListView.view.width
            height: row.height
            count: selection.revision >= 0 && selection.isSelected(drag.index) ? selection.trackCount
                                                                               : selection.rowTrackCount(drag.index)
            query: drag.dragging ? selection.query() : ""
            tracks: drag.dragging && !selection.queryOnly ? selection.tracks() : []
            onDraggingChanged: {
                if (drag.dragging)
                    selection.ensureSelected(drag.index)
            }

            TrackRow {
                id: row
                width: parent.width
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: selection.revision >= 0 && selection.isSelected(drag.index)
                columns: view.columns
                title: drag.model.title
                artist: drag.model.artist
                album: drag.model.year > 0 ? String(drag.model.year) : ""
                sampleRate: String(drag.model.trackCount)
                duration: drag.model.durationText
                format: drag.model.formatText
                coverSource: drag.model.coverUrl
                // 封面的 Accessible.name 是專輯名（album 槽借放年份）
                coverName: drag.model.title
                playing: !!view.source && view.source.currentAlbumId > 0
                         && Number(drag.model.albumId) === Number(view.source.currentAlbumId)
                onClicked: ListActions.click(view.actions, drag.index)
                onDoubleClicked: view.openRequested(drag.index)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: ListActions.openMenuAt(view.actions, drag.index)
                }
            }
        }
    }
}
