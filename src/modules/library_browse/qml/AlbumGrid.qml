pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Lib
import "ListActions.js" as ListActions

// 專輯的封面牆（S-03，AC-10-1、2）：每格是封面、專輯名（一行，截斷時 ToolTip）、藝人與等寬的格式摘要。
// 欄數隨寬度變（格寬 160 到 220）。GridView 只建立可見範圍（加上 cacheBuffer）的格子，
// 封面由 Cover 依格子大小非同步載入縮圖（image://cover），沒有封面時是預設圖。
// 正在播放的專輯封面外加 1px ink 框；hover 或鍵盤選到時封面上出現播放鍵＝播放整張專輯。
// 鍵盤焦點在格線本身（方向鍵、Enter 開專輯詳細），格子不另外取得焦點。
// T-25：以整張專輯為單位選取（Shift＋方向鍵、Ctrl＋點擊、Ctrl+A；選取的格子 surfaceRaised 底）、
// Shift+Enter 接著播放、Ctrl+Enter 加到佇列最後、右鍵選單、拖放。
GridView {
    id: grid
    objectName: "albumGrid"

    signal openRequested(int row)

    // 專輯的來源：LibraryAlbums（專輯頁）或 LibraryArtists（藝人頁，T-23）；
    // 要有 albums、currentAlbumId、playAlbum(row, from)
    property var source: LibraryAlbums

    // 介面規範 S-03 的格寬範圍（沒有對應的 token，見模組規格整合待辦）
    readonly property int minCell: 160
    readonly property int maxCell: 220
    readonly property int columns: {
        const w = width - leftMargin - rightMargin
        let n = Math.max(1, Math.ceil(w / maxCell))
        if (n > 1 && w / n < minCell)
            n -= 1
        return n
    }
    readonly property int coverSize: Math.max(0, Math.floor(cellWidth) - 2 * Theme.space2)

    clip: true
    // 關閉程式時單例可能比頁面先被拆掉（source 變 null），繫結要防呆
    model: source ? source.albums : null
    leftMargin: Theme.space4
    rightMargin: Theme.space4
    topMargin: Theme.space4
    bottomMargin: Theme.space4
    cellWidth: Math.floor((width - leftMargin - rightMargin) / columns)
    cellHeight: coverSize + Theme.space2 + Theme.lineBody + Theme.lineSmall + Theme.space4
    // 往上下各多建一列，捲動時封面先開始載入；不要太大，快速捲動才不會一次建太多格子
    cacheBuffer: cellHeight
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationWraps: false
    currentIndex: -1
    Accessible.role: Accessible.List
    Accessible.name: qsTr("專輯")
    ScrollBar.vertical: ScrollBar {}
    // 一格滾輪捲「系統行數」列格子
    WheelScroll { flickable: grid; lineHeight: grid.cellHeight }

    readonly property alias selection: trackSelection
    readonly property alias menu: trackMenu
    /// 狀態列左側的選取說明（頁面的 statusTextLeft）
    readonly property string selectionText: ListActions.statusText(trackSelection.trackCount)

    TrackSelection {
        id: trackSelection
        model: grid.model
    }

    TrackMenu {
        id: trackMenu
    }

    readonly property var actions: ({
        list: grid,
        selection: trackSelection,
        menu: trackMenu,
        player: Player,
        contain: GridView.Contain,
        rowStep: grid.columns,
        colStep: 1,
        activate: row => grid.openRequested(row)
    })

    Keys.onPressed: event => event.accepted = ListActions.handleKey(grid.actions, event)
    onActiveFocusChanged: {
        if (activeFocus && currentIndex < 0 && count > 0)
            currentIndex = 0
    }

    // 格子本身是拖放來源（TrackDragArea 包住整格）；內容是選取的整張專輯
    delegate: TrackDragArea {
        id: cell

        required property int index
        required property string title
        required property string artist
        required property string formatText
        required property string coverUrl
        required property var albumId

        readonly property bool current: GridView.isCurrentItem && grid.activeFocus
        readonly property bool playing: !!grid.source && grid.source.currentAlbumId > 0
                                        && Number(albumId) === Number(grid.source.currentAlbumId)

        readonly property bool selected: trackSelection.revision >= 0 && trackSelection.isSelected(cell.index)

        width: grid.cellWidth
        height: grid.cellHeight
        Accessible.role: Accessible.ListItem
        Accessible.name: [title, artist, formatText].filter(part => part.length > 0).join(" ")

        count: cell.selected ? trackSelection.trackCount : trackSelection.rowTrackCount(cell.index)
        query: cell.dragging ? trackSelection.query() : ""
        tracks: cell.dragging && !trackSelection.queryOnly ? trackSelection.tracks() : []
        onDraggingChanged: {
            if (cell.dragging)
                trackSelection.ensureSelected(cell.index)
        }

        HoverHandler {
            id: hover
        }
        TapHandler {
            onTapped: ListActions.click(grid.actions, cell.index)
            onDoubleTapped: grid.openRequested(cell.index)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: ListActions.openMenuAt(grid.actions, cell.index)
        }

        // 選取的格子：surfaceRaised 底（同選取列）
        Rectangle {
            anchors.fill: parent
            visible: cell.selected
            color: Theme.surfaceRaised
            radius: Theme.radius0
        }

        Cover {
            id: cover
            x: Theme.space2
            y: Theme.space2
            size: grid.coverSize
            source: cell.coverUrl
            albumName: cell.title

            // 正在播放的專輯：1px ink 框
            Rectangle {
                anchors.fill: parent
                visible: cell.playing
                color: Theme.transparent
                border.width: Theme.hairline
                border.color: Theme.ink
            }

            FocusFrame {
                visible: cell.current
            }

            IconButton {
                objectName: "albumPlay"
                anchors.centerIn: parent
                variant: "round"
                iconName: "play"
                text: qsTr("播放專輯「%1」").arg(cell.title)
                // 鍵盤操作走格線（Enter 開詳細）；播放鍵只給滑鼠，不進 Tab 順序
                activeFocusOnTab: false
                visible: hover.hovered || cell.current
                onClicked: grid.source.playAlbum(cell.index)
            }
        }

        Text {
            id: titleText
            anchors { left: cover.left; right: cover.right; top: cover.bottom; topMargin: Theme.space2 }
            text: cell.title
            font: Theme.body
            color: Theme.ink
            elide: Text.ElideRight
            maximumLineCount: 1

            ToolTip {
                visible: hover.hovered && titleText.truncated
                text: cell.title
            }
        }

        Text {
            anchors { left: cover.left; right: format.left; rightMargin: Theme.space2; top: titleText.bottom }
            text: cell.artist
            font: Theme.small
            color: Theme.inkMuted
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            id: format
            anchors { right: cover.right; top: titleText.bottom }
            text: cell.formatText
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
    }
}
