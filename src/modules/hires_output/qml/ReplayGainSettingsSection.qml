import QtQuick
import Auralis.Ui

// 設定頁的「ReplayGain」分區（畫面規格 S-16，AC-06-1、2）：模式與防止削波，即時生效。
// 每一項是「左邊名稱與一行說明、右邊控制項」；下方小字是目前這首的狀態（和輸出面板的區塊同一份）。
// 最下面是「計算整個音樂庫的 ReplayGain」（T-38，單例 R128）：計算中顯示進度與「取消」。
SettingsSection {
    id: section

    objectName: "replayGainSettingsSection"
    title: qsTr("ReplayGain")
    sectionId: "replayGain"

    Item {
        width: section.width
        height: Math.max(modeLabel.implicitHeight, modes.implicitHeight)

        Column {
            id: modeLabel
            anchors.left: parent.left
            anchors.right: modes.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("ReplayGain 模式")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("讓不同專輯的音量一致。開啟後不是位元完美；DSD 曲目不作用。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        SegmentedControl {
            id: modes
            objectName: "replayGainSettingsModes"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("ReplayGain 模式")
            model: ReplayGain.options
            // 選中項一律由設定算出（輸出面板改了也跟著變）。控制項點選時會自己寫 currentIndex，
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

    Checkbox {
        objectName: "replayGainPreventClipping"
        width: section.width
        text: qsTr("防止削波")
        description: qsTr("增益不超過曲目的峰值允許的量，套用後不超過 0 dBFS。")
        checked: ReplayGain.preventClipping
        onToggled: {
            ReplayGain.setPreventClipping(checked)
            checked = Qt.binding(() => ReplayGain.preventClipping)
        }
    }

    Text {
        objectName: "replayGainSettingsStatus"
        width: section.width
        visible: ReplayGain.status.length > 0
        text: ReplayGain.status
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }

    // R128 計算（AC-06-3）：背景工作，可取消；結果只存在 AuralisPlayer 的資料庫，不寫音樂檔
    Item {
        width: section.width
        height: Math.max(computeLabel.implicitHeight, computeButtons.implicitHeight)

        Column {
            id: computeLabel
            anchors.left: parent.left
            anchors.right: computeButtons.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("計算 ReplayGain")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("用 EBU R128 量測響度，結果存在 AuralisPlayer 的資料庫，不改動音樂檔。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        Row {
            id: computeButtons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2
            Button {
                objectName: "replayGainComputeLibrary"
                text: qsTr("計算整個音樂庫的 ReplayGain")
                busy: R128.running
                onClicked: R128.computeLibrary()
            }
            Button {
                objectName: "replayGainComputeCancel"
                visible: R128.running
                text: qsTr("取消")
                onClicked: R128.cancel()
            }
        }
    }

    ProgressLine {
        objectName: "replayGainComputeProgress"
        visible: R128.running
        width: section.width
        value: R128.progress
        label: qsTr("ReplayGain 計算進度")
    }

    Text {
        objectName: "replayGainComputeStatus"
        width: section.width
        visible: R128.status.length > 0
        text: R128.status
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
}
