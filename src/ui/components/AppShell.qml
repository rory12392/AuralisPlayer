pragma ComponentBehavior: Bound
import QtQuick

// 主視窗骨架（介面規範 §4）：側欄、內容區（工具列、頁面、狀態列）、右側面板、播放列、通知列。
// 頁面、側欄區塊與面板頁籤由 UiRegistry 登記、以 Loader 載入：載入失敗只讓那一塊顯示錯誤區塊。
Item {
    id: shell

    // 播放列的內容（主視窗放 shell/Playbar.qml）
    property url playbarSource
    readonly property bool fullscreen: Navigator.fullscreen
    readonly property LoggingCategory log: LoggingCategory {
        name: "auralis.ui"
    }

    // 空白鍵＝播放／暫停（介面規範 §7）：不用 Shortcut，焦點所在的按鈕、文字框、清單列沒有用掉空白鍵時
    // 才傳到這裡，所以按鈕的空白鍵觸發與文字輸入照常
    Keys.onSpacePressed: event => {
        if (event.isAutoRepeat)
            return
        Player.togglePause()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        onActivated: Player.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        onActivated: Player.previous()
    }
    // 輸出面板（畫面規格 S-14）在播放列的輸出按鈕上
    Shortcut {
        sequence: "Ctrl+O"
        enabled: !shell.fullscreen
        onActivated: Output.panelOpen = !Output.panelOpen
    }
    Shortcut {
        sequence: "Ctrl+I"
        onActivated: ShellState.panelOpen = !ShellState.panelOpen
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: Navigator.open("settings")
    }
    Shortcut {
        sequences: [StandardKey.Back]
        onActivated: Navigator.back()
    }
    // 面板切到歌詞（收合時先展開）；沒有登記歌詞面板（LYR 停用）時 showPanel 不做事
    Shortcut {
        sequence: "Ctrl+L"
        enabled: !shell.fullscreen
        onActivated: Navigator.showPanel("panel/lyrics")
    }
    // 進入／離開全螢幕；沒有登記全螢幕頁時不做事
    Shortcut {
        sequence: "F11"
        enabled: Navigator.fullscreenPage.length > 0
        onActivated: Navigator.toggleFullscreen()
    }
    // Esc 離開全螢幕：和空白鍵一樣不用 Shortcut，全螢幕頁自己用掉的 Esc（例：關掉頁面裡的浮層）
    // 不會傳到這裡；空白鍵、左右鍵也是全螢幕頁先拿到
    Keys.onEscapePressed: event => {
        if (shell.fullscreen)
            Navigator.leaveFullscreen()
        else
            event.accepted = false
    }
    // 全螢幕頁一律用 Void 的 token（DESIGN §2.3、T-94）：全螢幕期間有效主題是 Void，
    // 和切頁在同一個訊號裡換好，不會畫出一幀主題不對的畫面；不改使用者的 ui/theme 設定
    Binding {
        target: ThemeController
        property: "forceVoid"
        value: shell.fullscreen
    }
    onFullscreenChanged: {
        // 離開全螢幕後按鍵回到殼（空白鍵＝播放／暫停）
        if (!shell.fullscreen)
            shell.forceActiveFocus()
    }
    // Navigator.showPanel：展開面板並切到那個頁籤
    Connections {
        target: Navigator
        function onPanelRequested(panelId) {
            ShellState.panelOpen = true
            panel.select(panelId)
        }
    }

    Sidebar {
        id: sidebar
        visible: !shell.fullscreen
        anchors { left: parent.left; top: parent.top; bottom: notice.top }
    }

    Item {
        id: content
        visible: !shell.fullscreen
        anchors {
            left: sidebar.right
            right: panel.left
            top: parent.top
            bottom: notice.top
        }

        Rectangle {
            id: toolbar
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.headerHeight
            color: Theme.bg

            // 模組登記的常駐項目（addToolbarItem，例：LIB 的搜尋框），在頁面工具列左邊、每一頁都有。
            // 寬度照項目的 implicitWidth，但整排最多佔工具列可用寬度的一半，窄視窗時頁面工具列仍有空間。
            // 每一塊各自載入：載入失敗只讓那一塊不出現並寫日誌，頁面工具列照常
            Row {
                id: toolbarItems
                objectName: "toolbarItems"
                readonly property real maxWidth: (panelToggle.x - Theme.space6 - Theme.space4) / 2
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                anchors.leftMargin: Theme.space6
                spacing: Theme.space4

                Repeater {
                    model: UiRegistry.toolbarItems
                    delegate: Loader {
                        required property var modelData
                        readonly property Item box: item as Item
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        visible: status === Loader.Ready
                        // 每一塊最多佔一半（目前只有搜尋框一塊）
                        width: box ? Math.min(box.implicitWidth, toolbarItems.maxWidth) : 0
                        height: box ? box.implicitHeight : 0
                        source: modelData.source
                        onStatusChanged: {
                            if (status === Loader.Error)
                                console.warn(shell.log, "工具列項目載入失敗", modelData.id, modelData.source)
                        }
                    }
                }
            }
            Loader {
                id: toolbarLoader
                objectName: "pageToolbar"
                anchors { left: toolbarItems.right; right: panelToggle.left; top: parent.top; bottom: parent.bottom }
                anchors.leftMargin: toolbarItems.width > 0 ? Theme.space4 : 0
                sourceComponent: pageLoader.page ? pageLoader.page.toolbar ?? null : null
            }
            IconButton {
                id: panelToggle
                anchors.right: parent.right
                anchors.rightMargin: Theme.space6
                anchors.verticalCenter: parent.verticalCenter
                size: 40
                iconName: "panel"
                text: qsTr("正在播放面板")
                checkable: true
                checked: ShellState.panelOpen
                onToggled: ShellState.panelOpen = checked
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.hairline
                color: Theme.line
            }
        }

        PageArea {
            id: pageLoader
            anchors { left: parent.left; right: parent.right; top: toolbar.bottom; bottom: statusBar.top }
            source: shell.fullscreen ? "" : Navigator.currentSource
        }

        Rectangle {
            id: statusBar
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.statusBarHeight
            color: Theme.bg
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: Theme.hairline
                color: Theme.line
            }
            // 左邊：頁面的說明文字（statusTextLeft，例：S-07「已選取 <n> 首…」），是一般文字
            Text {
                id: pageStatusLeft
                objectName: "pageStatusLeft"
                visible: text.length > 0
                anchors.left: parent.left
                anchors.leftMargin: Theme.space6
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(0, statusRight.x - Theme.space6 - x))
                text: pageLoader.page ? pageLoader.page.statusTextLeft ?? "" : ""
                font: Theme.small
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
            // 輸出的說明（啟動時記住的輸出裝置不在、改用預設裝置）；是一般文字，不用等寬字
            Text {
                objectName: "outputNotice"
                visible: text.length > 0
                anchors.left: pageStatusLeft.visible ? pageStatusLeft.right : parent.left
                anchors.leftMargin: Theme.space6
                anchors.verticalCenter: parent.verticalCenter
                text: Output.notice
                font: Theme.small
                color: Theme.inkMuted
            }
            // 右邊：頁面的狀態文字（statusText，等寬，例：S-07「<n> 首符合」），再右邊是掃描進度；
            // 兩個都有時並排，掃描進度不被頁面文字蓋掉
            Row {
                id: statusRight
                objectName: "statusRight"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space6
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space6

                Text {
                    id: pageStatus
                    objectName: "pageStatus"
                    visible: text.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: pageLoader.page ? pageLoader.page.statusText ?? "" : ""
                    font: Theme.monoSmall
                    color: Theme.inkMuted
                }
                // 掃描中（介面規範 §6、AC-08-1）：不擋畫面，狀態列右邊顯示進度與已掃描數／總數
                Row {
                    id: scanStatus
                    objectName: "scanStatus"
                    visible: Library.scanning
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space3
                    ProgressLine {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.space24
                        value: Library.progress
                        indeterminate: Library.totalCount === 0
                        label: qsTr("掃描進度")
                    }
                    Text {
                        text: Library.progressText.length > 0 ? qsTr("已掃描 %1").arg(Library.progressText)
                                                              : qsTr("正在列出檔案")
                        font: Theme.monoSmall
                        color: Theme.inkMuted
                    }
                }
                // 其他背景工作（介面規範 §6：R128、歌詞查詢）：執行超過約 300 ms 的才出現，顯示最早的一個
                // 加「＋n 個」。名稱在寬度不夠時省略，整排不超出狀態列
                Row {
                    id: jobStatus
                    objectName: "jobStatus"
                    visible: Jobs.active
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space3
                    // 狀態列右邊扣掉頁面狀態、掃描進度與這一塊的其他部分後，名稱還能用的寬度
                    readonly property real nameRoom: statusBar.width - Theme.space6 * 2
                        - (pageStatus.visible ? pageStatus.width + statusRight.spacing : 0)
                        - (scanStatus.visible ? scanStatus.width + statusRight.spacing : 0)
                        - jobLine.width - jobCancel.width - spacing * 2
                        - (jobMore.visible ? jobMore.width + spacing : 0)
                    ProgressLine {
                        id: jobLine
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.space24
                        value: Jobs.progress
                        indeterminate: Jobs.indeterminate
                        label: Jobs.name
                    }
                    Text {
                        objectName: "jobName"
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, Math.min(implicitWidth, jobStatus.nameRoom))
                        text: Jobs.name
                        font: Theme.small
                        color: Theme.inkMuted
                        elide: Text.ElideRight
                    }
                    Text {
                        id: jobMore
                        objectName: "jobMore"
                        visible: Jobs.moreCount > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("＋%1 個").arg(Jobs.moreCount)
                        font: Theme.small
                        color: Theme.inkMuted
                    }
                    Button {
                        id: jobCancel
                        objectName: "jobCancel"
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.chipHeight
                        text: qsTr("取消")
                        Accessible.name: qsTr("取消%1").arg(Jobs.name)
                        onClicked: Jobs.cancel()
                    }
                }
            }
        }
    }

    SidePanel {
        id: panel
        // 截圖時主程式用這個名稱找到它切頁籤
        objectName: "sidePanel"
        visible: !shell.fullscreen && ShellState.panelOpen
        width: visible ? Theme.panelWidth : 0
        anchors { right: parent.right; top: parent.top; bottom: notice.top }
    }

    NoticeBar {
        id: notice
        anchors { left: parent.left; right: parent.right; bottom: playbar.top }
    }

    Rectangle {
        id: playbar
        visible: !shell.fullscreen
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: visible ? Theme.barHeight : 0
        color: Theme.surface
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.hairline
            color: Theme.line
        }
        Loader {
            id: playbarLoader
            anchors.fill: parent
            source: shell.playbarSource
        }
        Text {
            visible: playbarLoader.status !== Loader.Ready
            anchors.centerIn: parent
            text: qsTr("尚未播放")
            font: Theme.body
            color: Theme.inkMuted
        }
    }

    // 全螢幕頁蓋住整個視窗（例：全螢幕正在播放）；載入後鍵盤焦點給頁面，按鍵頁面先處理
    PageArea {
        objectName: "fullscreenArea"
        anchors.fill: parent
        visible: shell.fullscreen
        source: shell.fullscreen ? Navigator.currentSource : ""
        onItemChanged: {
            if (item)
                item.forceActiveFocus()
        }
    }

    DialogHost {}
}
