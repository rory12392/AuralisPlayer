import QtQuick
import Auralis.Ui

// 輸出面板的「ReplayGain」區塊（畫面規格 S-14，AC-06-1、AC-02-6）：標籤＋SegmentedControl
// 「關閉」「音軌」「專輯」，換了立即生效。下方小字是目前這首的狀態（增益與來源、退回音軌增益、
// 沒有 ReplayGain 資訊；DoP 與原生 DSD 播放中寫「DSD 直通時不作用」）。
Column {
    id: section

    objectName: "replayGainPanelSection"
    spacing: Theme.space2

    Item {
        width: section.width
        height: Math.max(label.implicitHeight, modes.implicitHeight)

        Text {
            id: label
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("ReplayGain")
            font: Theme.label
            color: Theme.inkMuted
        }
        SegmentedControl {
            id: modes
            objectName: "replayGainPanelModes"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("ReplayGain")
            model: ReplayGain.options
            // 選中項一律由設定算出（設定頁改了也跟著變）。控制項點選時會自己寫 currentIndex，
            // 用 Binding 而不是屬性綁定，才不會被覆寫斷開；點選只寫設定，再對回設定
            Binding on currentIndex {
                value: ReplayGain.modeIndex
            }
            onActivated: index => {
                ReplayGain.selectMode(index)
                currentIndex = ReplayGain.modeIndex
            }
        }
    }
    Text {
        objectName: "replayGainPanelStatus"
        width: section.width
        visible: ReplayGain.status.length > 0
        text: ReplayGain.status
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
}
