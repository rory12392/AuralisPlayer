pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui

// 可重用的歌詞清單：歌詞面板（S-12）用，全螢幕（T-68）沿用、字級與顏色由外面給。
// 同步歌詞時目前行（activeLine）保持在清單高度的 anchorRatio（40%）處，換行時以 motionPanel 平滑捲動；
// 使用者自己捲動（拖曳、滾輪、捲軸、上下鍵）後暫停自動置中，resumeDelay（3 秒）沒有操作再恢復。
// 點一行或在那一行按 Enter 發出 lineActivated(index)（同步歌詞才發，AC-15-3）；Tab 可到、上下鍵移動。
ListView {
    id: list

    property var lines: []
    // 目前播放的行；-1 表示第一行之前或不是同步歌詞
    property int activeLine: -1
    // false：純文字歌詞，不高亮、不置中、點了不跳
    property bool synced: true
    property font activeFont: Theme.heading
    property font lineFont: Theme.body
    property color activeColor: Theme.ink
    property color lineColor: Theme.inkMuted
    property real anchorRatio: 0.4
    property int resumeDelay: 3000
    // false 時捲軸一律不顯示（全螢幕的控制列隱藏時）
    property bool scrollBarAllowed: true
    // 自動置中中（沒有被使用者的捲動暫停）
    readonly property bool following: !pauseTimer.running

    signal lineActivated(int index)

    // 依 activeLine 捲到目標位置；animated 為 false 時直接跳過去（換歌、改變大小）
    function center(animated) {
        if (!list.synced || list.count === 0 || list.height <= 0)
            return
        scrollAnim.stop()
        let target = list.originY
        if (list.activeLine >= 0) {
            // 目前行可能還沒建立：先定位過去量出位置，再回到原處做動畫
            const saved = list.contentY
            list.positionViewAtIndex(list.activeLine, ListView.Beginning)
            const item = list.itemAtIndex(list.activeLine)
            target = list.contentY + (item ? item.height / 2 : 0) - list.height * list.anchorRatio
            list.contentY = saved
        }
        const bottom = Math.max(list.originY, list.originY + list.contentHeight - list.height)
        target = Math.max(list.originY, Math.min(bottom, target))
        if (animated && Theme.motionPanel > 0 && list.visible) {
            scrollAnim.from = list.contentY
            scrollAnim.to = target
            scrollAnim.start()
        } else {
            list.contentY = target
        }
    }

    function userScrolled() {
        scrollAnim.stop()
        pauseTimer.restart()
    }

    model: list.lines
    clip: true
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationWraps: false
    Accessible.role: Accessible.List
    Accessible.name: qsTr("歌詞")
    // 使用者的捲動：拖曳與甩動（movementStarted）、滾輪、拖捲軸、上下鍵與換頁鍵。
    // 不用 contentY 的變化判斷，因為換歌與改變大小時清單自己也會動
    ScrollBar.vertical: ScrollBar {
        objectName: "lyricsScrollBar"
        // 沉浸頁不要常駐的線：捲動中（含使用者捲動後暫停置中的期間）或滑鼠在清單上才顯示
        readonly property bool shown: list.scrollBarAllowed && (active || listHover.hovered || !list.following)
        opacity: shown ? 1 : 0
        // 隱藏時不接受輸入：否則看不見的捲軸會攔下右緣的點擊與拖曳（M-05 審查修正第 2 輪）
        interactive: shown
        Behavior on opacity {
            NumberAnimation { duration: Theme.motionState }
        }
        onPressedChanged: if (pressed) list.userScrolled()
    }
    HoverHandler {
        id: listHover
    }
    WheelScroll {
        id: wheelScroll
        flickable: list
    }
    Connections {
        target: wheelScroll
        function onWheel() { list.userScrolled() }
    }

    // 同步歌詞上下留白，第一行與最後一行也能停在 40% 處
    header: Item {
        width: list.width
        height: list.synced ? list.height * list.anchorRatio : Theme.space4
    }
    footer: Item {
        width: list.width
        height: list.synced ? list.height * (1 - list.anchorRatio) : Theme.space4
    }

    delegate: Item {
        id: row

        required property int index
        required property string modelData
        readonly property bool active: list.synced && row.index === list.activeLine

        width: list.width
        height: label.implicitHeight + Theme.space2 * 2
        Accessible.role: Accessible.ListItem
        Accessible.name: row.modelData

        Text {
            id: label
            x: Theme.space6
            width: row.width - Theme.space6 * 2
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData
            wrapMode: Text.Wrap
            font: row.active ? list.activeFont : list.lineFont
            color: row.active ? list.activeColor : list.lineColor
        }
        // 鍵盤焦點所在的行（上下鍵移動的是焦點，不是播放位置）
        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.focusWidth
            visible: list.activeFocus && row.index === list.currentIndex
            color: Theme.transparent
            border.color: Theme.focus
            border.width: Theme.focusWidth
        }
        HoverHandler {
            cursorShape: list.synced ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        TapHandler {
            onTapped: {
                list.currentIndex = row.index
                if (list.synced)
                    list.lineActivated(row.index)
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_PageUp
                || event.key === Qt.Key_PageDown)
            list.userScrolled()
        event.accepted = false // 移動本身交給 ListView
    }
    Keys.onReturnPressed: if (list.synced && list.currentIndex >= 0) list.lineActivated(list.currentIndex)
    Keys.onEnterPressed: if (list.synced && list.currentIndex >= 0) list.lineActivated(list.currentIndex)
    onActiveFocusChanged: {
        // Tab 進來時焦點從目前播放的那一行開始
        if (list.activeFocus && list.count > 0)
            list.currentIndex = Math.max(0, list.activeLine)
    }

    onMovementStarted: list.userScrolled()
    onActiveLineChanged: if (list.following) list.center(true)
    onHeightChanged: if (list.following) list.center(false)
    onLinesChanged: {
        // 換歌：清掉暫停，等清單重建後從頭對齊
        pauseTimer.stop()
        Qt.callLater(list.center, false)
    }

    NumberAnimation {
        id: scrollAnim
        target: list
        property: "contentY"
        duration: Theme.motionPanel
        easing.type: Easing.InOutQuad
    }
    Timer {
        id: pauseTimer
        interval: list.resumeDelay
        onTriggered: list.center(true)
    }
}
