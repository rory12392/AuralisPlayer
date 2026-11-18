pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import "TrackColumns.js" as TrackColumns

// 清單表頭（介面規範 §3）：和 TrackRow 用同一份 columns 才會對齊。可排序的欄（sortable 不是 false）
// 是按鈕，目前排序的欄顯示箭頭；按下發出 sortRequested(id)，由頁面決定新的排序。
// resizable 時欄與欄之間有分隔線可用滑鼠拖曳改欄寬（不進 Tab 順序；鍵盤使用者照預設欄寬）：拖曳中發出
// columnResized(id, width)，頁面用 TrackColumns.resized() 換掉自己的欄位清單（表頭與列才會一起動）；
// 放開時發出 columnResizeFinished(id, width)，頁面在這時用 TrackColumns.save() 保存。曲名欄（fill）
// 吃剩下的寬度，拖它右邊的分隔線改的是下一欄。元件本身不碰設定。
Rectangle {
    id: header

    property var columns: TrackColumns.defaultColumns()
    property bool showCover: true
    property string sortColumn
    property bool sortAscending: true
    property bool resizable: false
    signal sortRequested(string id)
    signal columnResized(string id, int width)
    signal columnResizeFinished(string id, int width)

    readonly property var columnWidths: TrackColumns.widths(columns, cells.width, Theme.space3)
    readonly property var columnEdges: TrackColumns.rightEdges(columnWidths, Theme.space3)

    implicitWidth: Theme.panelWidth * 2
    implicitHeight: Theme.rowHeightCompact
    color: Theme.bg

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.hairline
        color: Theme.line
    }

    Row {
        id: cells
        anchors.left: parent.left
        anchors.leftMargin: Theme.space6 + TrackColumns.markWidth
                            + (header.showCover ? Theme.coverThumb + Theme.space3 : Theme.space3)
        anchors.right: parent.right
        anchors.rightMargin: Theme.space6
        height: parent.height
        spacing: Theme.space3

        Repeater {
            model: header.columns
            delegate: T.Button {
                id: cell
                required property var modelData
                required property int index
                readonly property bool sorted: header.sortColumn === modelData.id
                readonly property bool alignRight: modelData.align === "right"

                objectName: "headerCell_" + modelData.id
                width: header.columnWidths[index] ?? 0
                visible: width > 0
                height: cells.height
                padding: 0
                enabled: modelData.sortable !== false
                activeFocusOnTab: enabled
                focusPolicy: Qt.StrongFocus
                text: modelData.title
                Accessible.name: sorted ? (header.sortAscending ? qsTr("%1，遞增排序").arg(text)
                                                                : qsTr("%1，遞減排序").arg(text))
                                        : text
                onClicked: header.sortRequested(modelData.id)

                contentItem: Item {
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: cell.alignRight ? undefined : parent.left
                        anchors.right: cell.alignRight ? parent.right : undefined
                        spacing: Theme.space1
                        layoutDirection: cell.alignRight ? Qt.RightToLeft : Qt.LeftToRight
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: cell.text
                            font: Theme.label
                            color: cell.sorted || cell.hovered ? Theme.ink : Theme.inkMuted
                        }
                        Icon {
                            visible: cell.sorted
                            anchors.verticalCenter: parent.verticalCenter
                            name: header.sortAscending ? "sort-asc" : "sort-desc"
                            size: 16
                        }
                    }
                }

                background: Item {
                    FocusFrame {
                        visible: cell.visualFocus
                    }
                }
            }
        }
    }

    // 欄寬分隔線（蓋在欄的按鈕上面，放在兩欄間距的正中間）
    Repeater {
        model: header.resizable ? header.columns.length : 0
        delegate: MouseArea {
            id: divider
            required property int index
            readonly property real edge: header.columnEdges[index] ?? -1
            property int target: -1
            property int sign: 1
            property real startX
            property int startWidth
            property int minWidth
            property int maxWidth
            property int lastWidth

            objectName: "columnDivider_" + (header.columns[index]?.id ?? "")
            visible: edge >= 0 && TrackColumns.nextShown(header.columnWidths, index) >= 0
            x: cells.x + edge + Theme.space3 / 2 - width / 2
            width: Theme.space2
            height: header.height
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SplitHCursor

            onPressed: mouse => {
                const resize = TrackColumns.resizeTarget(header.columns, header.columnWidths, index)
                target = resize.index
                sign = resize.sign
                if (target < 0)
                    return
                const limits = TrackColumns.resizeLimits(header.columns, header.columnWidths, target, cells.width,
                                                         Theme.space3)
                minWidth = limits.min
                maxWidth = limits.max
                startWidth = header.columnWidths[target]
                lastWidth = startWidth
                startX = mapToItem(header, mouse.x, 0).x
            }
            onPositionChanged: mouse => {
                if (!pressed || target < 0)
                    return
                const delta = mapToItem(header, mouse.x, 0).x - startX
                const width = Math.round(Math.max(minWidth, Math.min(maxWidth, startWidth + sign * delta)))
                if (width === lastWidth)
                    return
                lastWidth = width
                header.columnResized(header.columns[target].id, width)
            }
            // 放開或被取消（寬度已經跟著拖曳改了）都算拖完
            function finish() {
                if (target >= 0 && lastWidth !== startWidth)
                    header.columnResizeFinished(header.columns[target].id, lastWidth)
                target = -1
            }
            onReleased: finish()
            onCanceled: finish()

            Rectangle {
                anchors.centerIn: parent
                width: Theme.hairline
                height: parent.height / 2
                color: divider.containsMouse || divider.pressed ? Theme.lineStrong : Theme.line
            }
        }
    }
}
