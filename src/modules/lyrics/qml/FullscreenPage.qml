import QtQuick
import Auralis.Ui

// 全螢幕正在播放（畫面規格 S-15，頁面 id fullscreen，US-17）。主題由設定 LYR/fullscreenTheme 決定
// （深色／淺色／跟隨主程式，AC-17-6）：頁面只宣告 fullscreenTheme，核心照它套用，fs* token 跟著有效主題。
// 上列（64）：「正在播放」、按鍵說明與深淺色切換鈕；中段：左邊大封面（視窗高的 54%，最大 720）、曲名、「<藝人>　<專輯>」，
// 右邊歌詞（目前行 display／fsInk 垂直置中，其他行 lyric／fsInkMuted；點一行跳轉）；下列（96）：進度與時間、
// 輸出狀態。沒有歌詞、純音樂、線上暫時取不到時右欄不顯示，封面與曲名置中。
// 按鍵（AC-17-3）：核心載入後把焦點給這個根物件；空白鍵播放／暫停、左右鍵上一首／下一首在這裡處理，
// 進度條有焦點時左右鍵由進度條用掉（跳轉 5 秒）。Esc 不接，傳到殼離開全螢幕。
// 滑鼠 3 秒不動隱藏游標與上下兩列（AC-17-4），移動時以 motionPanel 淡入；鍵盤操作不讓它們出現，
// 只有進度條有焦點時上下兩列保持顯示（鍵盤使用者看得到自己在調什麼）。
// 開在哪個螢幕由核心決定（主視窗中心所在的螢幕），這一頁不設 screen。
Item {
    id: page

    objectName: "fullscreenPage"
    // 這一頁要的主題（AC-17-6）：主視窗殼把它繫到 ThemeController，進全螢幕與切換時同一個呼叫裡換好
    readonly property string fullscreenTheme: Lyrics.fullscreenTheme
    // 底色：深色＝封面主色壓暗（C++ 算好給 Lyrics，沒有封面時 #050505）；淺色＝封面主色提亮（核心算，
    // 沒有封面時 Paper 的 bg）。換歌時以 motionTint 過渡（AC-17-2）；換深淺色的那一刻 motionTint 是 0，直接換
    property color backdrop: Theme.dark ? Lyrics.fullscreenBackdrop
                                        : ThemeController.lightBackdrop(Lyrics.fullscreenCoverColor)
    // 滑鼠多久不動算閒置（毫秒）
    property int idleDelay: 3000
    readonly property bool idle: idleState.idle
    readonly property bool controlsShown: !page.idle || progress.activeFocus || themeButton.activeFocus
    // 右欄要不要顯示歌詞；載入中維持上一首的版面，結果出來再換（切歌時不先跳一次）
    // （不寫成繫結：繫結在「載入中」時也會重算）
    property bool showLyrics: false
    readonly property int coverSize: Math.min(720, Math.round(page.height * 0.54))

    function lyricsVisibleIn(state) {
        return state === "synced" || state === "plain"
    }
    // 滑鼠動了（或剛進全螢幕）：顯示上下兩列與游標，重新計時
    function wake() {
        idleState.idle = false
        idleTimer.restart()
    }
    // 只有位置真的變了才算動（版面淡入淡出時 Qt 會補送同一個位置的 hover）
    function pointerAt(x, y) {
        if (x === idleState.lastX && y === idleState.lastY)
            return
        idleState.lastX = x
        idleState.lastY = y
        page.wake()
    }
    // T 鍵與切換鈕：依目前看到的主題換成另一個（跟隨主程式時也改成明確的值，S-15）
    function toggleTheme() {
        Lyrics.setFullscreenTheme(Theme.dark ? "light" : "dark")
    }

    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("全螢幕正在播放")

    Component.onCompleted: page.showLyrics = page.lyricsVisibleIn(Lyrics.state)
    Keys.onPressed: event => {
        // Ctrl+←／→ 等組合鍵交給殼（方向鍵可能帶 KeypadModifier，不算組合鍵）
        if ((event.modifiers & ~Qt.KeypadModifier) !== Qt.NoModifier)
            return
        if (event.key === Qt.Key_Space) {
            event.accepted = true
            if (!event.isAutoRepeat)
                Player.togglePause()
        } else if (event.key === Qt.Key_Left) {
            event.accepted = true
            Player.previous()
        } else if (event.key === Qt.Key_Right) {
            event.accepted = true
            Player.next()
        } else if (event.key === Qt.Key_T) {
            event.accepted = true
            if (!event.isAutoRepeat)
                page.toggleTheme()
        }
        // Esc 與其他按鍵不接：Esc 傳到殼（離開全螢幕）
    }

    Connections {
        target: Lyrics
        function onChanged() {
            if (Lyrics.state !== "loading")
                page.showLyrics = page.lyricsVisibleIn(Lyrics.state)
        }
    }

    QtObject {
        id: idleState
        property bool idle: false
        property real lastX: -1
        property real lastY: -1
    }
    Timer {
        id: idleTimer
        interval: page.idleDelay
        running: true
        onTriggered: idleState.idle = true
    }
    HoverHandler {
        onPointChanged: page.pointerAt(point.position.x, point.position.y)
    }

    Rectangle {
        objectName: "fsBackdrop"
        anchors.fill: parent
        color: page.backdrop

        // 整面單色、只用 QML 動畫（AC-17-5，不用自訂 shader）；過渡中又換色時從目前顯示的顏色接續
        Behavior on color {
            ColorAnimation {
                duration: Theme.motionTint
                easing.type: Easing.InOutQuad
            }
        }
    }

    // 中段：上下兩列隱藏時不重排（位置固定在兩列之間）
    Item {
        id: middle
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.space12
        anchors.rightMargin: Theme.space12

        Column {
            id: info
            objectName: "fsInfo"
            width: page.coverSize
            anchors.verticalCenter: parent.verticalCenter
            x: page.showLyrics ? 0 : (middle.width - width) / 2
            spacing: Theme.space4

            Cover {
                objectName: "fsCover"
                size: page.coverSize
                source: Player.coverUrl
                albumName: Player.album
            }
            Text {
                objectName: "fsTitle"
                width: parent.width
                topPadding: Theme.space2
                text: Player.hasTrack ? Player.title : qsTr("尚未播放")
                font: Theme.title
                color: Theme.fsInk
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: page.showLyrics ? Text.AlignLeft : Text.AlignHCenter
            }
            Text {
                objectName: "fsArtistAlbum"
                width: parent.width
                visible: text.length > 0
                text: [Player.artist, Player.album].filter(part => part.length > 0).join("　")
                font: Theme.heading
                color: Theme.fsInkMuted
                elide: Text.ElideRight
                horizontalAlignment: page.showLyrics ? Text.AlignLeft : Text.AlignHCenter
            }
        }

        LyricsList {
            id: lyrics
            objectName: "fsLyrics"
            anchors.left: info.right
            anchors.leftMargin: Theme.space12
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: page.showLyrics
            lines: Lyrics.lines
            activeLine: Lyrics.currentLine
            synced: Lyrics.state === "synced"
            activeFont: Theme.display
            lineFont: Theme.lyric
            activeColor: Theme.fsInk
            lineColor: Theme.fsInkMuted
            anchorRatio: 0.5
            scrollBarAllowed: page.controlsShown
            onLineActivated: index => Lyrics.seekToLine(index)
        }

        // 線上歌詞暫時取不到（AC-16-2）：照「沒有歌詞」的版面，右下角一行小字
        Text {
            objectName: "fsUnavailable"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: Lyrics.state === "unavailable"
            text: qsTr("線上歌詞暫時無法取得")
            font: Theme.small
            color: Theme.fsInkMuted
        }
    }

    Item {
        id: topBar
        objectName: "fsTopBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.space12 + Theme.space4
        opacity: page.controlsShown ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motionPanel
                easing.type: Easing.InOutQuad
            }
        }

        Text {
            objectName: "fsTopLabel"
            anchors.left: parent.left
            anchors.leftMargin: Theme.space12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("正在播放")
            font: Theme.label
            color: Theme.fsInkMuted
        }
        Text {
            objectName: "fsKeyHints"
            anchors.right: themeButton.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            text: [qsTr("空白鍵 播放／暫停"), qsTr("← → 切歌"), qsTr("T 深淺色"), qsTr("Esc 離開")].join("　　")
            font: Theme.monoSmall
            color: Theme.fsInkMuted
        }
        // 深淺色切換鈕（AC-17-6）：兩態，結果寫進設定。滑鼠按了不搶焦點（空白鍵照舊是播放／暫停），
        // Tab 走得到；有焦點時上下兩列保持顯示
        IconButton {
            id: themeButton
            objectName: "fsThemeButton"
            anchors.right: parent.right
            anchors.rightMargin: Theme.space12
            anchors.verticalCenter: parent.verticalCenter
            iconName: "contrast"
            text: Theme.dark ? qsTr("切換成淺色") : qsTr("切換成深色")
            focusPolicy: Qt.TabFocus
            onClicked: page.toggleTheme()
        }
    }

    Item {
        id: bottomBar
        objectName: "fsBottomBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space12
        anchors.rightMargin: Theme.space12
        height: Theme.space24
        opacity: page.controlsShown ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motionPanel
                easing.type: Easing.InOutQuad
            }
        }

        Text {
            id: positionText
            objectName: "fsPosition"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            // 拖曳時即時顯示拖到的時間
            text: Player.formatTime(progress.pressed ? progress.value : Player.positionMs)
            font: Theme.monoSmall
            color: Theme.fsInkMuted
        }
        Slider {
            id: progress
            objectName: "fsProgress"
            anchors.left: positionText.right
            anchors.leftMargin: Theme.space3
            anchors.right: duration.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            variant: "progress"
            label: qsTr("播放進度")
            enabled: Player.durationMs > 0
            from: 0
            to: Math.max(Player.durationMs, 1)
            // 不設 stepSize：設了滑鼠的落點會被取到級距上；有焦點時左右鍵一次 5 秒（不切歌）
            keyStep: 5000
            value: Player.positionMs

            // 同播放列：按下時斷開和播放位置的繫結，放開（或按完鍵）時跳轉並接回
            function commit() {
                Player.seek(Math.round(value))
                value = Qt.binding(() => Player.positionMs)
            }
            onPressedChanged: {
                if (pressed)
                    value = value
                else
                    commit()
            }
            onMoved: if (!pressed) commit()
        }
        Text {
            id: duration
            objectName: "fsDuration"
            anchors.right: output.left
            anchors.rightMargin: Theme.space8
            anchors.verticalCenter: parent.verticalCenter
            text: Player.formatTime(Player.durationMs)
            font: Theme.monoSmall
            color: Theme.fsInkMuted
        }

        // 輸出狀態：等寬「<格式>　<取樣率> / <位元深度>　<輸出路徑>」＋位元完美或原因
        Row {
            id: output
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space4

            Text {
                objectName: "fsOutputText"
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const codec = Player.readouts.length > 0 ? Player.readouts[0].value : ""
                    const parts = Output.active ? [codec, Output.formatText, Output.pathName] : []
                    return parts.filter(part => part && part !== "—").join("　")
                }
                font: Theme.monoSmall
                color: Theme.fsInkMuted
            }
            StatusTag {
                objectName: "fsStatusTag"
                anchors.verticalCenter: parent.verticalCenter
                text: Output.statusText
                variant: Output.statusVariant
            }
        }
    }

    // 閒置時蓋在最上面藏起游標（歌詞行的手形游標也蓋掉）；不收按鍵，點擊照常傳下去
    MouseArea {
        objectName: "fsCursorShield"
        anchors.fill: parent
        visible: page.idle
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.BlankCursor
        onPositionChanged: mouse => page.pointerAt(mouse.x, mouse.y)
    }
}
