pragma ComponentBehavior: Bound
import QtQuick

// 正在播放面板（畫面規格 S-11，AC-10-6）：封面（面板寬扣內距的正方形）、曲名、藝人（可點：藝人頁）、
// 專輯與年份（可點：專輯詳細頁）、
// 「曲目 <n> / <總數>」；髮絲線；四格讀數（格式、取樣率、位元深度、位元率，DSD 時換成
// 格式、DSD 倍率、輸出方式、位元率）；髮絲線；模組登記的區塊。面板本身由主視窗殼收合（Ctrl+I、工具列的面板開關）。
// 歌詞預覽屬於歌詞模組（登記成區塊），模組停用時不顯示。
Item {
    id: panel

    // 點藝人、點專輯前往該頁（T-27）：目前曲目在音樂庫的專輯裡，而且那一頁有登記（LIB 停用時沒有）
    readonly property bool inAlbum: Player.albumId > 0
    readonly property LoggingCategory log: LoggingCategory {
        name: "auralis.ui"
    }
    function hasPage(pageId) {
        return UiRegistry.pageIds.indexOf(pageId) >= 0
    }

    // 可點的文字（連結）：平常和一般文字一樣，滑鼠移上去畫 1px 底線、游標變手形；鍵盤焦點畫焦點框，
    // Tab 可到、Enter 與空白鍵觸發。linked 為 false 時就是一般文字：不能點、不進 Tab 順序。
    // 殼層不能匯入 Templates（token 檢查），用 TapHandler／HoverHandler 自己處理點擊與鍵盤。
    component NowPlayingLink: Item {
        id: link

        property string text
        property bool linked: false
        property color textColor: Theme.ink
        readonly property alias hoverCursor: hover.cursorShape
        signal clicked

        width: Math.min(implicitWidth, parent.width)
        implicitWidth: label.implicitWidth
        implicitHeight: label.implicitHeight
        visible: text.length > 0
        // 換曲後不能點了：先把焦點拿掉（有焦點時 Qt 不讓 activeFocusOnTab 變成 false）
        activeFocusOnTab: false
        Component.onCompleted: activeFocusOnTab = linked
        onLinkedChanged: {
            if (!linked && activeFocus)
                focus = false
            activeFocusOnTab = linked
        }
        Accessible.role: linked ? Accessible.Link : Accessible.StaticText
        Accessible.focusable: linked
        Accessible.onPressAction: link.activate()

        function activate() {
            if (link.linked)
                link.clicked()
        }
        Keys.onReturnPressed: link.activate()
        Keys.onEnterPressed: link.activate()
        Keys.onSpacePressed: link.activate()

        Text {
            id: label
            width: link.width
            text: link.text
            font: Theme.body
            color: link.textColor
            elide: Text.ElideRight
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.hairline
            visible: link.linked && hover.hovered
            color: link.textColor
        }
        FocusFrame {
            visible: link.linked && link.activeFocus
        }
        HoverHandler {
            id: hover
            enabled: link.linked
            cursorShape: link.linked ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        TapHandler {
            enabled: link.linked
            onTapped: link.activate()
        }
    }

    Flickable {
        id: flick
        objectName: "nowPlayingContent"
        anchors.fill: parent
        visible: Player.hasTrack
        clip: true
        contentWidth: width
        contentHeight: column.height + 2 * Theme.space6
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: flick }

        Column {
            id: column
            x: Theme.space6
            y: Theme.space6
            width: flick.width - 2 * Theme.space6
            spacing: Theme.space2

            // 點封面＝進全螢幕（同 F11，鍵盤用 F11）；沒有登記全螢幕頁（LYR 停用）時不是可點的
            Cover {
                id: cover
                objectName: "nowPlayingCover"
                readonly property bool opensFullscreen: Navigator.fullscreenPage.length > 0
                readonly property alias hoverCursor: coverHover.cursorShape
                size: column.width
                source: Player.coverUrl
                albumName: Player.album

                HoverHandler {
                    id: coverHover
                    enabled: cover.opensFullscreen
                    cursorShape: cover.opensFullscreen ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    enabled: cover.opensFullscreen
                    onTapped: Navigator.enterFullscreen()
                }
            }

            Item { width: 1; height: Theme.space2 }

            Text {
                objectName: "nowPlayingTitle"
                width: parent.width
                text: Player.title
                font: Theme.heading
                color: Theme.ink
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            NowPlayingLink {
                objectName: "nowPlayingArtistLink"
                text: Player.artist
                textColor: Theme.ink
                linked: panel.inAlbum && panel.hasPage("library/artists")
                Accessible.name: linked ? qsTr("前往藝人 %1").arg(text) : text
                onClicked: Navigator.open("library/artists", { artist: Player.artist })
            }
            NowPlayingLink {
                objectName: "nowPlayingAlbumLink"
                text: Player.albumLine
                textColor: Theme.inkMuted
                linked: panel.inAlbum && panel.hasPage("library/album")
                Accessible.name: linked ? qsTr("前往專輯 %1").arg(Player.album) : text
                onClicked: Navigator.open("library/album", { albumId: Player.albumId })
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: Player.trackText
                font: Theme.small
                color: Theme.inkMuted
            }

            Item { width: 1; height: Theme.space2 }
            Rectangle {
                width: parent.width
                height: Theme.hairline
                color: Theme.line
            }
            Item { width: 1; height: Theme.space2 }

            // 四格讀數，兩欄排
            Grid {
                objectName: "nowPlayingReadouts"
                width: parent.width
                columns: 2
                rowSpacing: Theme.space4
                columnSpacing: Theme.space4

                Repeater {
                    model: Player.readouts
                    delegate: Readout {
                        required property var modelData
                        width: (column.width - Theme.space4) / 2
                        label: modelData.label
                        value: modelData.value
                        unit: modelData.unit
                    }
                }
            }

            Item { width: 1; height: Theme.space2 }
            Rectangle {
                width: parent.width
                height: Theme.hairline
                color: Theme.line
            }

            // 模組登記的區塊（addNowPlayingSection，例：LYR 的歌詞預覽），在髮絲線之後依 order 排。
            // 沒有登記時什麼都不加；每一塊各自載入，載入失敗只讓那一塊不出現並寫日誌
            Repeater {
                model: UiRegistry.nowPlayingSections
                delegate: Loader {
                    required property var modelData
                    readonly property Item box: item as Item
                    width: column.width
                    height: box ? box.implicitHeight : 0
                    visible: status === Loader.Ready
                    source: modelData.source
                    onStatusChanged: {
                        if (status === Loader.Error)
                            console.warn(panel.log, "正在播放面板區塊載入失敗", modelData.id, modelData.source)
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.fill: parent
        visible: !Player.hasTrack
        title: qsTr("尚未播放")
        description: qsTr("在音樂庫雙擊曲目或專輯開始播放。")
    }
}
