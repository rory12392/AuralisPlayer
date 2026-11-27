pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib
import "ListActions.js" as ListActions

// 專輯的詳細檢視（S-03）：依專輯分組的曲目清單。左側大封面（160）與專輯資訊，右側該專輯的曲目列
// （曲序、曲名、長度、格式；TrackRow 不帶封面，album 槽放曲序）。鍵盤焦點在外層清單（上下鍵換專輯、
// Enter 開專輯詳細）；雙擊曲目＝佇列換成整張專輯、從那一首開始播（AC-10-4 的做法，詳細頁是 T-24）。
// T-25：以整張專輯為單位選取、Shift+Enter／Ctrl+Enter、右鍵選單、拖放（同 AlbumList）；在曲目列上按右鍵
// 時選單只對那一首。
ListView {
    id: list
    objectName: "albumDetailList"

    signal openRequested(int row)

    // 專輯的來源：LibraryAlbums 或 LibraryArtists（T-23），見 AlbumGrid.source
    property var source: LibraryAlbums

    readonly property int coverSize: 160
    // 960 寬（頁面寬 < 760）時拿掉曲目的「格式」欄，曲名才放得下（格式摘要仍在左側專輯資訊）
    readonly property bool compact: width < 760
    readonly property var trackColumns: {
        const all = [
            { id: "album", title: qsTr("曲序"), width: 32, align: "right" },
            { id: "title", title: qsTr("曲名"), weight: 1 },
            { id: "duration", title: qsTr("長度"), width: 64, align: "right" },
            { id: "sampleRate", title: qsTr("格式"), width: 136 }
        ]
        return compact ? all.filter(c => c.id !== "sampleRate") : all
    }

    clip: true
    // 關閉程式時單例可能比頁面先被拆掉（source 變 null），繫結要防呆
    model: source ? source.albums : null
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationWraps: false
    currentIndex: -1
    cacheBuffer: coverSize * 2
    Accessible.role: Accessible.List
    Accessible.name: qsTr("專輯")
    ScrollBar.vertical: ScrollBar {}
    // 一組專輯的高度不固定：一格滾輪捲「系統行數」列曲目（組內的曲目列是緊湊列）
    WheelScroll { flickable: list; lineHeight: Theme.rowHeightCompact }

    readonly property alias selection: trackSelection
    /// 狀態列左側的選取說明（頁面的 statusTextLeft）
    readonly property string selectionText: ListActions.statusText(trackSelection.trackCount)

    TrackSelection {
        id: trackSelection
        model: list.model
    }

    TrackMenu {
        id: menu
    }

    readonly property var actions: ({
        list: list,
        selection: trackSelection,
        menu: menu,
        player: Player,
        contain: ListView.Contain,
        rowStep: 1,
        colStep: 0,
        activate: row => list.openRequested(row)
    })

    Keys.onPressed: event => event.accepted = ListActions.handleKey(list.actions, event)
    onActiveFocusChanged: {
        if (activeFocus && currentIndex < 0 && count > 0)
            currentIndex = 0
    }

    // 一組（一張專輯）是拖放來源，內容是選取的整張專輯
    delegate: TrackDragArea {
        id: group

        required property int index
        required property var model

        readonly property bool current: ListView.isCurrentItem && list.activeFocus
        readonly property bool selected: trackSelection.revision >= 0 && trackSelection.isSelected(group.index)

        width: ListView.view.width
        height: Math.max(list.coverSize, trackColumn.height) + 2 * Theme.space4 + Theme.hairline
        Accessible.role: Accessible.ListItem
        Accessible.name: [group.model.title, group.model.artist].filter(part => part.length > 0).join(" ")

        count: group.selected ? trackSelection.trackCount : trackSelection.rowTrackCount(group.index)
        query: group.dragging ? trackSelection.query() : ""
        tracks: group.dragging && !trackSelection.queryOnly ? trackSelection.tracks() : []
        onDraggingChanged: {
            if (group.dragging)
                trackSelection.ensureSelected(group.index)
        }

        TapHandler {
            onTapped: ListActions.click(list.actions, group.index)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: ListActions.openMenuAt(list.actions, group.index)
        }

        // 選取的專輯：surfaceRaised 底（同選取列）
        Rectangle {
            anchors.fill: parent
            visible: group.selected
            color: Theme.surfaceRaised
        }

        Cover {
            id: cover
            x: Theme.space6
            y: Theme.space4
            size: list.coverSize
            source: group.model.coverUrl
            albumName: group.model.title

            Rectangle {
                anchors.fill: parent
                visible: !!list.source && list.source.currentAlbumId > 0
                         && Number(group.model.albumId) === Number(list.source.currentAlbumId)
                color: Theme.transparent
                border.width: Theme.hairline
                border.color: Theme.ink
            }

            FocusFrame {
                visible: group.current
            }
        }

        Column {
            id: info
            anchors { left: cover.right; leftMargin: Theme.space4; top: cover.top }
            width: Math.min(Theme.panelWidth / 2 + Theme.space24, group.width / 4)
            spacing: Theme.space1

            Text {
                width: parent.width
                text: group.model.title
                font: Theme.bodyMedium
                color: Theme.ink
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: group.model.artist
                font: Theme.small
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: [group.model.year > 0 ? String(group.model.year) : "",
                       qsTr("%1 首").arg(group.model.trackCount),
                       group.model.durationText].filter(part => part.length > 0).join(" · ")
                font: Theme.monoSmall
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: group.model.formatText
                font: Theme.monoSmall
                color: Theme.inkMuted
            }
        }

        Column {
            id: trackColumn
            anchors { left: info.right; leftMargin: Theme.space4; right: parent.right; top: cover.top }

            Repeater {
                model: group.model.tracks
                delegate: TrackRow {
                    id: trackRow
                    required property int index
                    required property var modelData

                    width: trackColumn.width
                    showCover: false
                    activeFocusOnTab: false
                    focusPolicy: Qt.NoFocus
                    columns: list.trackColumns
                    album: modelData.trackNo > 0 ? String(modelData.trackNo) : ""
                    title: modelData.title
                    duration: modelData.durationText
                    sampleRate: modelData.qualityText
                    playing: LibraryPlayback.currentKey.length > 0
                             && LibraryPlayback.currentKey === LibraryPlayback.keyOf(modelData.path, modelData.subsong)
                    onClicked: ListActions.click(list.actions, group.index)
                    onDoubleClicked: list.source.playAlbum(group.index, trackRow.index)

                    // 在曲目列上按右鍵：只對這一首（前往專輯、開啟檔案位置才用得到）
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            list.currentIndex = group.index
                            list.forceActiveFocus()
                            menu.popupAt(TrackDragCodec.encode([{ path: trackRow.modelData.path,
                                                                  subsong: trackRow.modelData.subsong }], "", 1), 1)
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.hairline
            color: Theme.line
        }
    }
}
