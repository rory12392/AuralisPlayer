import QtQuick
import Auralis.Ui

// 輸出面板的「DSD 輸出」區塊（畫面規格 S-14，AC-04-2、AC-02-6）：標籤＋SegmentedControl
// 「DoP」「原生 DSD」「轉 PCM <取樣率>」。不能用的方式停用並把原因寫在選項上；選的方式在目前輸出不能用
// 而改用別的方式時下方小字說明；裝置不支援時下方用 signal 寫原因與建議。換了立即生效，DSD 播放中
// 從目前位置以新方式繼續。目前選的輸出路徑從裝置清單告訴 DsdOutput（換輸出時跟著換包裝器）。
Column {
    id: section

    objectName: "dsdPanelSection"
    spacing: Theme.space2

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
        height: Math.max(label.implicitHeight, modes.implicitHeight)

        Text {
            id: label
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("DSD 輸出")
            font: Theme.label
            color: Theme.inkMuted
        }
        Text {
            anchors.left: label.right
            anchors.leftMargin: Theme.space3
            anchors.right: modes.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            visible: DsdOutput.routeLabel.length > 0
            text: qsTr("播放中：%1").arg(DsdOutput.routeLabel)
            font: Theme.monoSmall
            color: Theme.inkMuted
            elide: Text.ElideRight
        }
        SegmentedControl {
            id: modes
            objectName: "dsdPanelModes"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("DSD 輸出")
            model: DsdOutput.options
            // 選中項一律由實際使用的方式算出（設定頁、換輸出都跟著變）。控制項點選時會自己寫
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
    Text {
        width: section.width
        visible: DsdOutput.note.length > 0
        text: DsdOutput.note
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
    // 沒有 ASIO 外掛：「原生 DSD」停用（原因寫在選項上），這裡帶到設定頁的「外掛」分區（T-104）
    Item {
        objectName: "dsdPanelPluginHint"
        visible: !DsdOutput.nativeDsdAvailable
        width: section.width
        height: Math.max(pluginHint.implicitHeight, pluginLink.implicitHeight)
        Text {
            id: pluginHint
            anchors.left: parent.left
            anchors.right: pluginLink.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("原生 DSD 需要 ASIO 外掛，可以在設定頁匯入。")
            font: Theme.small
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }
        Button {
            id: pluginLink
            objectName: "dsdPanelPluginLink"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("外掛設定")
            onClicked: {
                Output.panelOpen = false
                Navigator.open("settings", {"section": "plugins"})
            }
        }
    }
    Text {
        objectName: "dsdPanelWarning"
        width: section.width
        visible: DsdOutput.warning.length > 0
        text: DsdOutput.warning
        font: Theme.small
        color: Theme.signal
        wrapMode: Text.Wrap
    }
}
