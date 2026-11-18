import QtQuick

// 播放列（畫面規格 S-02、介面規範 §4）：左邊播放／暫停、停止、上一首、下一首；中間封面縮圖（面板收合時）、
// 曲名與藝人、隨機與重複、進度條與時間；右邊是輸出按鈕（OutputButton，放在 outputSlot）與音量。
// 資料與操作都經 Player（PlaybackService）。沒有曲目時中段寫「尚未播放」，佇列也是空的時傳輸鍵停用。
// 快捷鍵（空白鍵、Ctrl+←／→）在主視窗殼處理，焦點在哪裡都能用。
Item {
    id: bar

    // 播放列橫跨整個視窗；和正在播放面板自動收合的寬度（1200）相同
    readonly property bool wide: width >= 1200
    readonly property string repeatLabel: Player.repeatMode === "all" ? qsTr("全部")
                                          : Player.repeatMode === "one" ? qsTr("單曲") : qsTr("關")

    implicitHeight: Theme.barHeight

    Row {
        id: transport
        anchors.left: parent.left
        anchors.leftMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2

        // 順序照 Rory 指定（2026-11-17）：播放／暫停、停止、上一首、下一首
        IconButton {
            objectName: "playButton"
            anchors.verticalCenter: parent.verticalCenter
            variant: "round"
            iconName: Player.playing ? "pause" : "play"
            text: Player.playing ? qsTr("暫停") : qsTr("播放")
            enabled: Player.canPlay
            onClicked: Player.togglePause()
        }
        // 停止（AC-02-8）：關閉輸出裝置，播放列回到「尚未播放」；已停止或沒有曲目時停用
        IconButton {
            objectName: "stopButton"
            anchors.verticalCenter: parent.verticalCenter
            size: 40
            iconName: "stop"
            text: qsTr("停止")
            enabled: Player.canStop
            onClicked: Player.stop()
        }
        IconButton {
            objectName: "previousButton"
            anchors.verticalCenter: parent.verticalCenter
            size: 40
            iconName: "previous"
            text: qsTr("上一首")
            enabled: Player.canPlay
            onClicked: Player.previous()
        }
        IconButton {
            objectName: "nextButton"
            anchors.verticalCenter: parent.verticalCenter
            size: 40
            iconName: "next"
            text: qsTr("下一首")
            enabled: Player.canPlay
            onClicked: Player.next()
        }
    }

    // 輸出按鈕的位置（S-02 右側）：顯示輸出路徑、裝置、取樣率與位元完美；
    // 窄視窗時只留取樣率與狀態點（介面規範 §4），所以寬度跟著視窗寬度變
    // 音量（S-02 最右側，AC-02-7）：標籤「音量」、滑桿、數值。100% 以外時輸出狀態變成非位元完美
    // （輸出按鈕寫「音量 n%」）；DSD 直通播放中停用，滑鼠移上去說明原因。窄視窗不顯示標籤。
    Item {
        id: volumeArea
        objectName: "volumeArea"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        width: volumeRow.width
        height: Theme.controlHeight

        Row {
            id: volumeRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2

            Text {
                visible: bar.wide
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("音量")
                font: Theme.label
                color: Theme.inkMuted
            }
            Slider {
                id: volume
                objectName: "volumeSlider"
                anchors.verticalCenter: parent.verticalCenter
                width: bar.wide ? Theme.space24 : Theme.space12 + Theme.space4
                label: qsTr("音量")
                Accessible.description: Player.volumeUnavailableReason
                enabled: Player.volumeAvailable
                from: 0
                to: 100
                // 不設 stepSize：設了滑鼠的落點會被取到級距上；左右鍵一次 5%
                keyStep: 5
                value: Player.volume

                // 拖曳與方向鍵都立即生效；使用者動過之後 value 不再跟著服務，放開（或按完鍵）時接回
                onMoved: {
                    Player.volume = Math.round(value)
                    if (!pressed)
                        value = Qt.binding(() => Player.volume)
                }
                onPressedChanged: {
                    if (pressed)
                        value = value
                    else
                        value = Qt.binding(() => Player.volume)
                }
            }
            Text {
                objectName: "volumeText"
                anchors.verticalCenter: parent.verticalCenter
                // 固定寬度（放得下「100%」），數值變動時滑桿不位移
                width: Theme.space8 + Theme.space1
                horizontalAlignment: Text.AlignRight
                text: qsTr("%1%").arg(Player.volume)
                font: Theme.monoSmall
                color: Player.volumeAvailable ? Theme.inkMuted : Theme.inkFaint
            }
        }

        // 停用的滑桿收不到滑鼠事件，hover 由外層偵測
        HoverHandler {
            id: volumeHover
        }
        ToolTip {
            objectName: "volumeToolTip"
            visible: !Player.volumeAvailable && volumeHover.hovered
            text: Player.volumeUnavailableReason
        }
    }

    Item {
        id: outputSlot
        objectName: "outputSlot"
        anchors.right: volumeArea.left
        anchors.rightMargin: Theme.space4
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: bar.wide ? Theme.panelWidth : Theme.sidebarWidth / 2

        OutputButton {
            objectName: "outputButton"
            anchors.fill: parent
            anchors.topMargin: Theme.space4
            anchors.bottomMargin: Theme.space4
            compact: !bar.wide
        }
    }

    Cover {
        id: cover
        visible: !ShellState.panelOpen
        anchors.left: transport.right
        anchors.leftMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        size: Theme.coverBar
        // 已停止時和剛開程式一樣不帶封面（AC-02-8）
        source: Player.stopped ? "" : Player.coverUrl
        albumName: Player.stopped ? "" : Player.album
    }

    Item {
        id: middle
        anchors.left: cover.visible ? cover.right : transport.right
        anchors.leftMargin: Theme.space6
        anchors.right: outputSlot.left
        anchors.rightMargin: Theme.space6
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        // 沒有曲目，或已停止（AC-02-8：曲名、隨機與重複、進度條都收起來，不是歸零）
        Text {
            objectName: "idleText"
            visible: !nowPlaying.visible
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("尚未播放")
            font: Theme.body
            color: Theme.inkMuted
        }

        Item {
            id: nowPlaying
            objectName: "nowPlayingInfo"
            visible: Player.hasTrack && !Player.stopped
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: titleRow.height + Theme.space1 + progressRow.height

            Item {
                id: titleRow
                width: parent.width
                height: Theme.controlHeight

                Row {
                    anchors.left: parent.left
                    anchors.right: modes.left
                    anchors.rightMargin: Theme.space4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space3
                    clip: true

                    Text {
                        id: titleText
                        objectName: "titleText"
                        width: Math.min(implicitWidth, parent.width)
                        text: Player.title
                        font: Theme.bodyMedium
                        color: Theme.ink
                        elide: Text.ElideRight
                    }
                    Text {
                        width: Math.max(0, parent.width - titleText.width - parent.spacing)
                        text: Player.artist
                        font: Theme.body
                        color: Theme.inkMuted
                        elide: Text.ElideRight
                    }
                }

                Row {
                    id: modes
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space2

                    IconButton {
                        id: shuffleButton
                        objectName: "shuffleButton"
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "shuffle"
                        text: qsTr("隨機")
                        checkable: true
                        checked: Player.shuffle
                        // 按鈕自己切換會斷掉繫結；以服務的狀態為準
                        onToggled: {
                            Player.toggleShuffle()
                            checked = Qt.binding(() => Player.shuffle)
                        }
                    }
                    IconButton {
                        id: repeatButton
                        objectName: "repeatButton"
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: Player.repeatMode === "one" ? "repeat-one" : "repeat"
                        text: qsTr("重複：%1").arg(bar.repeatLabel)
                        checkable: true
                        checked: Player.repeatMode !== "off"
                        onToggled: {
                            Player.cycleRepeat()
                            checked = Qt.binding(() => Player.repeatMode !== "off")
                        }
                    }
                    // 重複的目前狀態寫成文字（S-02：關／全部／單曲）
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.repeatLabel
                        font: Theme.small
                        color: Theme.inkMuted
                    }
                }
            }

            Item {
                id: progressRow
                anchors.top: titleRow.bottom
                anchors.topMargin: Theme.space1
                width: parent.width
                height: Theme.hitMin

                Text {
                    id: positionText
                    objectName: "positionText"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // 拖曳時即時顯示拖到的時間
                    text: Player.formatTime(progress.pressed ? progress.value : Player.positionMs)
                    font: Theme.monoSmall
                    color: Theme.inkMuted
                }
                Slider {
                    id: progress
                    objectName: "progressSlider"
                    anchors.left: positionText.right
                    anchors.leftMargin: Theme.space3
                    anchors.right: durationText.left
                    anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "progress"
                    label: qsTr("播放進度")
                    enabled: Player.durationMs > 0
                    from: 0
                    to: Math.max(Player.durationMs, 1)
                    // 不設 stepSize：設了滑鼠的落點會被取到 5 秒的倍數上
                    keyStep: 5000
                    value: Player.positionMs

                    // 使用者拖曳或按方向鍵後 value 不再跟著播放位置；跳轉後接回繫結
                    function commit() {
                        Player.seek(Math.round(value))
                        value = Qt.binding(() => Player.positionMs)
                    }
                    // Slider 自己改 value（拖曳、點擊）不會斷開繫結，按住期間每次位置更新都會把把手
                    // 拉回播放位置；按下時先斷開，放開時跳轉並接回
                    onPressedChanged: {
                        if (pressed)
                            value = value
                        else
                            commit()
                    }
                    onMoved: if (!pressed) commit()
                }
                Text {
                    id: durationText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Player.formatTime(Player.durationMs)
                    font: Theme.monoSmall
                    color: Theme.inkMuted
                }
            }
        }
    }
}
