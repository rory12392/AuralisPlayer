pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁「清單與拖放」：ListHeader、TrackRow 的狀態、SidebarItem、TrackDragArea／TrackDropArea、ScrollBar。
// 曲目資料是展示用的範例，不是真的音樂庫。
Column {
    id: section

    property string lastDrop: qsTr("把上面的曲目拖到「通勤」")
    readonly property var sampleTracks: [
        // 範例的曲名、藝人是資料，不是介面文字，所以不包 qsTr（鍵名用 name 以免被當成介面文字檢查）
        { name: "Goldberg Variations, BWV 988: Aria", artist: "Glenn Gould", album: "Bach: Goldberg Variations",
          duration: "3:05", format: "FLAC", sampleRate: "96 kHz / 24 bit", playing: true },
        { name: "夜に駆ける", artist: "YOASOBI", album: "THE BOOK", duration: "4:21", format: "FLAC",
          sampleRate: "44.1 kHz / 16 bit", selected: true },
        { name: "Weight of the World", artist: "Keiichi Okabe", album: "NieR:Automata", duration: "5:39",
          format: "DSF", sampleRate: "DSD64" },
        { name: "遺されたもの", artist: "Kalafina", album: "Seventh Heaven", duration: "4:46", format: "FLAC",
          sampleRate: "48 kHz / 24 bit", missing: true }
    ]

    spacing: Theme.space6

    Panel {
        width: section.width
        title: "ListHeader／TrackRow"
        index: qsTr("%1 首").arg(section.sampleTracks.length)
        Column {
            width: parent.width
            ListHeader { width: parent.width; sortColumn: "title"; sortAscending: true }
            Repeater {
                model: section.sampleTracks
                delegate: TrackDragArea {
                    id: rowItem
                    required property var modelData
                    required property int index
                    width: parent.width
                    height: trackRow.height
                    tracks: [{ path: "C:\\Music\\" + rowItem.modelData.name + ".flac", subsong: 0 }]
                    TrackRow {
                        id: trackRow
                        width: parent.width
                        title: rowItem.modelData.name
                        artist: rowItem.modelData.artist
                        album: rowItem.modelData.album
                        duration: rowItem.modelData.duration
                        format: rowItem.modelData.format
                        sampleRate: rowItem.modelData.sampleRate
                        playing: rowItem.modelData.playing ?? false
                        highlighted: rowItem.modelData.selected ?? false
                        missing: rowItem.modelData.missing ?? false
                    }
                }
            }
            Item { width: 1; height: Theme.space4 }
            Text { text: qsTr("窄版（沒有封面、只留曲名、藝人、長度）"); font: Theme.label; color: Theme.inkMuted }
            TrackRow {
                width: Math.min(parent.width, Theme.panelWidth + Theme.space24)
                showCover: false
                columns: [{ id: "title", title: qsTr("曲名"), weight: 2 }, { id: "artist", title: qsTr("藝人"), weight: 1 },
                          { id: "duration", title: qsTr("長度"), width: 48, align: "right" }]
                title: "Weight of the World"
                artist: "Keiichi Okabe"
                duration: "5:39"
            }
        }
    }

    // T-89：欄位清單（自訂欄從 record 取值、表頭分隔線拖曳欄寬）；頁面自己保管清單，拖曳時換掉、放開時保存
    Panel {
        id: columnsPanel
        width: section.width
        title: qsTr("ListHeader／TrackRow：自訂欄與欄寬")
        index: qsTr("拖表頭的分隔線改欄寬")

        property var columns: [
            { id: "title", title: qsTr("曲名"), weight: 3 },
            { id: "artist", title: qsTr("藝人"), weight: 2 },
            { id: "composer", title: qsTr("作曲者"), weight: 2 },
            { id: "year", title: qsTr("年份"), width: 56, align: "right", mono: true },
            { id: "bitrate", title: qsTr("位元率"), width: 96, align: "right", mono: true,
              value: record => record ? qsTr("%1 kbps").arg(record.kbps) : "" },
            { id: "duration", title: qsTr("長度"), width: 64, align: "right" }
        ]
        property string saved: TrackColumns.save(columns)

        Column {
            width: parent.width
            ListHeader {
                width: parent.width
                resizable: true
                columns: columnsPanel.columns
                onColumnResized: (id, width) => columnsPanel.columns = TrackColumns.resized(columnsPanel.columns, id, width)
                onColumnResizeFinished: columnsPanel.saved = TrackColumns.save(columnsPanel.columns)
            }
            Repeater {
                // 範例資料（曲名、人名是資料，不是介面文字）
                model: [
                    { name: "Goldberg Variations, BWV 988: Aria", artist: "Glenn Gould", composer: "J. S. Bach",
                      year: 1981, kbps: 1411, duration: "3:05" },
                    { name: "夜に駆ける", artist: "YOASOBI", composer: "Ayase", year: 2019, kbps: 2304,
                      duration: "4:21" }
                ]
                delegate: TrackRow {
                    required property var modelData
                    width: parent.width
                    columns: columnsPanel.columns
                    record: modelData
                    title: modelData.name
                    artist: modelData.artist
                    duration: modelData.duration
                }
            }
            Text {
                width: parent.width
                topPadding: Theme.space2
                text: qsTr("保存：%1").arg(columnsPanel.saved)
                font: Theme.monoSmall
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
        }
    }

    Flow {
        width: section.width
        spacing: Theme.space6

        Panel {
            width: Theme.panelWidth
            title: "SidebarItem／TrackDropArea"
            Column {
                width: parent.width
                SidebarItem { width: parent.width; text: qsTr("專輯"); count: "1,204"; current: true }
                SidebarItem { width: parent.width; text: qsTr("藝人"); count: "386" }
                SidebarItem {
                    id: dropItem
                    width: parent.width
                    text: qsTr("通勤")
                    count: "48"
                    dropTarget: drop.containsDrag
                    TrackDropArea {
                        id: drop
                        anchors.fill: parent
                        onTracksDropped: payload => section.lastDrop = qsTr("已放下 %1 首：%2")
                                                       .arg(payload.count).arg(payload.tracks[0]?.path ?? payload.query)
                    }
                }
                SidebarItem { width: parent.width; text: qsTr("拖曳目標（靜態）"); dropTarget: true }
                Text {
                    width: parent.width
                    topPadding: Theme.space3
                    text: section.lastDrop
                    font: Theme.small
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                }
            }
        }

        Panel {
            width: Theme.panelWidth
            title: "TrackDragArea"
            Column {
                width: parent.width
                spacing: Theme.space2
                Text {
                    width: parent.width
                    text: qsTr("全選 40,112 首拖曳：只帶查詢條件")
                    font: Theme.small
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                }
                TrackDragArea {
                    width: parent.width
                    height: Theme.rowHeight
                    count: 40112
                    query: "all"
                    Rectangle {
                        anchors.fill: parent
                        color: Theme.surfaceRaised
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("拖這裡")
                            font: Theme.body
                            color: Theme.ink
                        }
                    }
                }
            }
        }

        Panel {
            width: Theme.panelWidth
            title: "ScrollBar"
            ListView {
                id: scrollDemo
                width: parent.width
                height: Theme.rowHeightCompact * 4
                clip: true
                model: 30
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: scrollDemo; lineHeight: Theme.rowHeightCompact }
                delegate: SidebarItem {
                    required property int index
                    width: ListView.view.width
                    text: qsTr("播放清單 %1").arg(index + 1)
                }
            }
        }
    }
}
