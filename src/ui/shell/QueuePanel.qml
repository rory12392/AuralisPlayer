pragma ComponentBehavior: Bound
import QtQuick

// 佇列面板（畫面規格 S-13）：頂部「<n> 首　<總長>」與「清空佇列」；清單分「正在播放」（一列）與「接下來」。
// 操作：雙擊或 Enter 跳到這首、Delete 移出佇列、拖曳把手或 Ctrl+↑／↓ 排序；從任何清單拖來的曲目
// （TrackDragArea）放在指到的位置。「清空佇列」直接做，通知列提供「復原」（由 Player.queue 處理）。
Item {
    id: panel

    readonly property QueueModel queue: Player.queue
    // 拖曳把手排序：從哪一列拖、目前指到哪一列（-1 表示沒有在拖）
    property int dragFrom: -1
    property int dragTo: -1

    function rowAtContentY(y) {
        const row = list.indexAt(Theme.space6, y)
        return row >= 0 ? row : (y < 0 ? 0 : list.count - 1)
    }

    // 拖來的曲目：游標在一列的上半部放在它前面，下半部放在它後面；指到清單下方的空白放最後
    function dropRow(x, y) {
        const point = dropArea.mapToItem(list.contentItem, x, y)
        const row = list.indexAt(Theme.space6, point.y)
        if (row < 0)
            return list.count
        const item = list.itemAtIndex(row)
        return item && point.y > item.y + item.height / 2 ? row + 1 : row
    }

    function moveCurrent(delta) {
        const target = list.currentIndex + delta
        panel.queue.moveTo(list.currentIndex, target)
        // 佇列改變時清單整個重建，選取跟著移動的那一列
        list.currentIndex = Math.max(0, Math.min(target, list.count - 1))
    }

    Item {
        id: queueHeader
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space6
        height: Theme.headerHeight

        Text {
            objectName: "queueSummary"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 首　%2").arg(Number(panel.queue.count).toLocaleString(Qt.locale(), "f", 0))
                      .arg(Player.formatTime(panel.queue.totalDurationMs))
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
        Button {
            objectName: "clearQueueButton"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("清空佇列")
            enabled: panel.queue.count > (panel.queue.hasCurrent ? 1 : 0)
            onClicked: panel.queue.clearQueue()
        }
    }

    ListView {
        id: list
        objectName: "queueList"
        anchors { left: parent.left; right: parent.right; top: queueHeader.bottom; bottom: parent.bottom }
        clip: true
        model: panel.queue
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationWraps: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("佇列")
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: list }

        Keys.onReturnPressed: panel.queue.playAt(currentIndex)
        Keys.onEnterPressed: panel.queue.playAt(currentIndex)
        Keys.onDeletePressed: {
            const row = currentIndex
            panel.queue.removeAt(row)
            currentIndex = Math.min(row, count - 1)
        }
        Keys.onUpPressed: event => {
            if (event.modifiers & Qt.ControlModifier)
                panel.moveCurrent(-1)
            else
                decrementCurrentIndex()
        }
        Keys.onDownPressed: event => {
            if (event.modifiers & Qt.ControlModifier)
                panel.moveCurrent(1)
            else
                incrementCurrentIndex()
        }

        section.property: "current"
        section.delegate: Text {
            required property string section
            width: ListView.view.width
            leftPadding: Theme.space6
            topPadding: Theme.space3
            bottomPadding: Theme.space2
            text: section === "true" ? qsTr("正在播放") : qsTr("接下來")
            font: Theme.label
            color: Theme.inkMuted
        }

        delegate: Item {
            id: entry
            required property int index
            required property string title
            required property string artist
            required property var durationMs
            required property bool current
            required property bool unplayable
            required property string coverUrl

            width: ListView.view.width
            height: Theme.rowHeight

            TrackRow {
                anchors.fill: parent
                // 右側留給拖曳把手（把手疊在列上，列的分隔線照常畫滿）
                rightPadding: Theme.space3 + handle.width
                // 鍵盤焦點在清單本身（上下鍵、Enter、Delete 由清單處理），列不另外取得焦點
                activeFocusOnTab: false
                focusPolicy: Qt.NoFocus
                highlighted: entry.ListView.isCurrentItem && list.activeFocus
                columns: [{ id: "title", title: qsTr("曲名"), weight: 3 }, { id: "artist", title: qsTr("藝人"), weight: 2 },
                          { id: "duration", title: qsTr("長度"), width: 48, align: "right" }]
                title: entry.title
                artist: entry.artist
                coverSource: entry.coverUrl
                duration: Player.formatTime(entry.durationMs)
                playing: entry.current
                statusText: entry.unplayable ? qsTr("無法播放") : ""
                onClicked: {
                    list.currentIndex = entry.index
                    list.forceActiveFocus()
                }
                onDoubleClicked: panel.queue.playAt(entry.index)
            }

            // 拖曳把手：拖到別的列放開＝排序（鍵盤用 Ctrl+↑／↓）。「正在播放」那列不能拖
            Item {
                id: handle
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                anchors.rightMargin: Theme.space3
                width: Theme.space8

                Icon {
                    anchors.centerIn: parent
                    visible: !entry.current
                    name: "drag-handle"
                    size: 16
                    tone: handleArea.containsMouse || handleArea.pressed ? "ink" : "faint"
                }
                MouseArea {
                    id: handleArea
                    objectName: "dragHandle"
                    anchors.fill: parent
                    enabled: !entry.current
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    onPressed: {
                        panel.dragFrom = entry.index
                        panel.dragTo = entry.index
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            panel.dragTo = panel.rowAtContentY(mapToItem(list.contentItem, mouse.x, mouse.y).y)
                    }
                    onReleased: {
                        const from = panel.dragFrom
                        const to = panel.dragTo
                        panel.dragFrom = -1
                        panel.dragTo = -1
                        if (from >= 0 && to >= 0 && to !== from) {
                            panel.queue.moveTo(from, to)
                            list.currentIndex = Math.min(to, list.count - 1)
                        }
                    }
                    onCanceled: {
                        panel.dragFrom = -1
                        panel.dragTo = -1
                    }
                }
            }

            // 排序時要放的位置：在目標列畫一條 ink 線（往上移畫在上緣，往下移畫在下緣）
            Rectangle {
                visible: panel.dragFrom >= 0 && panel.dragTo === entry.index && panel.dragTo !== panel.dragFrom
                anchors { left: parent.left; right: parent.right }
                anchors.leftMargin: Theme.space6
                anchors.rightMargin: Theme.space6
                y: panel.dragTo < panel.dragFrom ? 0 : parent.height - height
                height: 2 * Theme.hairline
                color: Theme.ink
            }
        }
    }

    EmptyState {
        anchors.fill: list
        visible: panel.queue.count === 0
        title: qsTr("佇列是空的")
        description: qsTr("在曲目上按右鍵選「加到佇列最後」。")
    }

    // 接受從任何清單拖來的曲目；拖曳經過時整個清單區畫 1px ink 框（拖曳目標）
    TrackDropArea {
        id: dropArea
        objectName: "queueDropArea"
        anchors.fill: list
        onTracksDropped: payload => panel.queue.insertTracks(payload.json, panel.dropRow(payload.x, payload.y))
    }
    Rectangle {
        anchors.fill: list
        visible: dropArea.containsDrag
        color: Theme.transparent
        border.width: Theme.hairline
        border.color: Theme.ink
    }
}
