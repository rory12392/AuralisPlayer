pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁「浮層」：Dialog、Menu／MenuItem、ToolTip、通知列（NoticeBar 在播放列上方，由 Notifier 驅動）。
// 開頁參數 open=menu、open=trackmenu 或 open=dialog 時直接打開，截圖用。
Flow {
    id: section

    readonly property int columnWidth: width >= 2 * Theme.panelWidth + Theme.space6
                                       ? Math.floor((width - Theme.space6) / 2) : width

    spacing: Theme.space6

    Component.onCompleted: {
        if (Navigator.params.open === "menu")
            trackMenu.popup(menuTrigger, 0, menuTrigger.height + Theme.space1)
        else if (Navigator.params.open === "trackmenu")
            demoTrackMenu.popupFrom(section.demoPayload(2), 2, trackMenuTrigger)
        else if (Navigator.params.open === "dialog")
            demoDialog.open()
    }

    Panel {
        width: section.columnWidth
        title: "Dialog"
        Row {
            spacing: Theme.space2
            Button { text: qsTr("顯示確認對話框"); onClicked: demoDialog.open() }
        }
        Dialog {
            id: demoDialog
            variant: "confirm"
            title: qsTr("刪除播放清單「通勤」")
            message: qsTr("只刪除播放清單，不會刪除音樂檔。")
            confirmText: qsTr("刪除播放清單")
        }
    }

    Panel {
        width: section.columnWidth
        title: "Menu／MenuItem"
        Button {
            id: menuTrigger
            text: qsTr("開啟曲目選單")
            onClicked: trackMenu.popup(menuTrigger, 0, menuTrigger.height + Theme.space1)
        }
        Menu {
            id: trackMenu
            MenuItem { text: qsTr("播放"); shortcut: "Enter" }
            MenuItem { text: qsTr("接著播放"); shortcut: "Shift+Enter" }
            MenuItem { text: qsTr("加到佇列最後"); shortcut: "Ctrl+Enter" }
            Menu {
                title: qsTr("加入播放清單")
                MenuItem { text: qsTr("通勤") }
                MenuItem { text: qsTr("新增播放清單") }
            }
            MenuItem { text: qsTr("從播放清單移除"); shortcut: "Delete"; enabled: false }
        }
    }

    // 展示用的曲目（不存在的路徑，所以沒有「開啟檔案位置」）
    function demoPayload(count) {
        const tracks = []
        for (let i = 0; i < count; ++i)
            tracks.push({ path: "C:/demo/" + (i + 1) + ".flac", subsong: 0 })
        return TrackDragCodec.encode(tracks, "", count)
    }

    Panel {
        width: section.columnWidth
        title: "TrackMenu"
        Row {
            spacing: Theme.space2
            Button {
                id: trackMenuTrigger
                text: qsTr("曲目右鍵選單（2 首）")
                onClicked: demoTrackMenu.popupFrom(section.demoPayload(2), 2, trackMenuTrigger)
                Keys.onPressed: event => {
                    if (demoTrackMenu.isOpenKey(event)) {
                        demoTrackMenu.popupFrom(section.demoPayload(2), 2, trackMenuTrigger)
                        event.accepted = true
                    }
                }
            }
            Button {
                id: queryMenuTrigger
                text: qsTr("全選 6,000 首")
                onClicked: demoTrackMenu.popupFrom(TrackDragCodec.encode([], "demo", 6000), 6000,
                                                   queryMenuTrigger)
            }
        }
        TrackMenu {
            id: demoTrackMenu
        }
    }

    Panel {
        width: section.columnWidth
        title: "Popover"
        Button {
            id: popoverTrigger
            text: qsTr("開啟浮層")
            onClicked: popover.opened ? popover.close() : popover.open()
            Popover {
                id: popover
                y: popoverTrigger.height + Theme.space1
                contentItem: Text {
                    padding: Theme.space4
                    text: qsTr("不是 modal：點外面或 Esc 關閉")
                    font: Theme.body
                    color: Theme.ink
                }
            }
        }
    }

    Panel {
        width: section.columnWidth
        title: "ToolTip"
        Item {
            width: parent.width
            height: Theme.space12 + Theme.space6
            IconButton {
                anchors.bottom: parent.bottom
                iconName: "fullscreen"
                text: qsTr("全螢幕（F11）")
                size: 40
            }
            // 靜態展示用：一直顯示的說明提示
            Item {
                x: Theme.space24 + Theme.space12
                anchors.bottom: parent.bottom
                width: Theme.space24
                height: Theme.controlHeight
                ToolTip {
                    text: qsTr("DSD 直通時無法調整音量")
                    visible: true
                    delay: 0
                }
            }
        }
    }

    Panel {
        width: section.columnWidth
        title: "NoticeBar"
        Row {
            spacing: Theme.space2
            Button {
                text: qsTr("一般通知")
                onClicked: Notifier.post(qsTr("已加入 3 首到「通勤」"))
            }
            Button {
                text: qsTr("錯誤通知")
                onClicked: Notifier.post(qsTr("無法播放：01 Aria.flac（檔案已損毀），已跳到下一首"), "error")
            }
        }
    }
}
