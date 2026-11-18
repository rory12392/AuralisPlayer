import QtQuick
import Auralis.Ui

// 設定頁的「DSD」分區（畫面規格 S-16，AC-04-2）：DSD 輸出方式與轉 PCM 的取樣率，即時生效。
// 每一項是「左邊名稱與一行說明、右邊控制項」；不能用的方式停用並寫原因，改用別的方式時小字說明，
// 裝置不支援時用 signal 寫原因與建議。資料都來自 DsdOutput（和輸出面板的區塊同一份）。
SettingsSection {
    id: section

    objectName: "dsdSettingsSection"
    title: qsTr("DSD")
    sectionId: "dsd"

    function syncOutputPath() {
        const devices = Output.devices
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].selected) {
                DsdOutput.setOutputPath(devices[i].path)
                return
            }
        }
    }

    Component.onCompleted: syncOutputPath()

    Connections {
        target: Output
        function onDevicesChanged() { section.syncOutputPath() }
    }

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
                text: qsTr("DSD 輸出方式")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("原生 DSD 只能用在 ASIO；轉 PCM 不是位元完美。播放中切換會從目前位置繼續。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        SegmentedControl {
            id: modes
            objectName: "dsdSettingsModes"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("DSD 輸出方式")
            model: DsdOutput.options
            // 選中項一律由實際使用的方式算出（輸出面板、換輸出都跟著變）。控制項點選時會自己寫
            // currentIndex，用 Binding 而不是屬性綁定，才不會被覆寫斷開；點選只寫設定，再對回實際的方式
            Binding on currentIndex {
                value: DsdOutput.modeIndex
            }
            onActivated: index => {
                DsdOutput.selectMode(index)
                currentIndex = DsdOutput.modeIndex
            }
        }
    }

    Item {
        width: section.width
        height: Math.max(rateLabel.implicitHeight, rates.implicitHeight)

        Column {
            id: rateLabel
            anchors.left: parent.left
            anchors.right: rates.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("轉 PCM 的取樣率")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("選轉 PCM 時以這個取樣率、24 bit 輸出。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        Select {
            id: rates
            objectName: "dsdPcmRate"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("取樣率")
            model: DsdOutput.pcmRateLabels
            currentIndex: DsdOutput.pcmRateIndex
            onActivated: index => {
                DsdOutput.selectPcmRate(index)
                currentIndex = Qt.binding(() => DsdOutput.pcmRateIndex)
            }
        }
    }

    Text {
        width: section.width
        visible: DsdOutput.note.length > 0
        text: DsdOutput.note
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
    Text {
        objectName: "dsdSettingsWarning"
        width: section.width
        visible: DsdOutput.warning.length > 0
        text: DsdOutput.warning
        font: Theme.small
        color: Theme.signal
        wrapMode: Text.Wrap
    }
}
