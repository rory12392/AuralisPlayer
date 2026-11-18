pragma ComponentBehavior: Bound
import QtQuick

// 輸出面板（畫面規格 S-14）：從輸出按鈕上方彈出的 Panel（標題「輸出」，右上「Ctrl O」），不是 modal。
// 裝置區：一列一個「輸出路徑＋裝置」，選了立即生效、從目前位置繼續播（AC-02-6）；開著時按 1、2、3…
// 直接選。之後是進階輸出模組登記的區塊（addOutputPanelSection：DSD 輸出、ReplayGain，模組停用時沒有）、
// 訊號路徑（來源 → 處理 → 送到 DAC）與說明。資料都來自 Output。
Popover {
    id: popover

    width: 2 * Theme.panelWidth

    // 面板從按鈕往上長，最高到視窗頂端（留 space2）：小視窗（960×640）加上模組的區塊時放不下，
    // 由裝置清單讓出高度（至少留 2 列，其餘捲動），標題與各區塊都留在視窗內
    // 面板與清單的高度來自兩層 Column 的排版；浮層裡的 Column 有時沒跟著內容重排（停在舊高度，例：
    // 裝置列在背景列舉完重建後清單仍是「沒有裝置」的高度），量出的其餘高度就錯、面板頂端跑出視窗。
    // 所以量之前與改完清單高度後都先排好，兩邊的高度才對得上
    function fitToWindow() {
        if (!popover.parent)
            return
        devices.forceLayout()
        content.forceLayout()
        const room = popover.parent.mapToItem(null, 0, 0).y - 2 * Theme.space2
        const others = panel.implicitHeight - deviceList.height
        deviceList.maxHeight = Math.max(2 * Theme.space12, room - others)
        content.forceLayout()
    }
    onAboutToShow: fitToWindow()

    // 開著時視窗大小或區塊的高度變了就重新量（量完的高度相同時不會再觸發）
    Connections {
        target: popover.parent ? popover.parent.Window.window : null
        function onHeightChanged() {
            if (popover.visible)
                Qt.callLater(popover.fitToWindow)
        }
    }
    Connections {
        target: panel
        function onImplicitHeightChanged() {
            if (popover.visible)
                Qt.callLater(popover.fitToWindow)
        }
    }
    // 裝置列是打開後在背景列舉、整批重建的：清單沒重排時面板高度不會變，上面那條不會觸發
    Connections {
        target: Output
        function onDevicesChanged() {
            if (popover.visible)
                Qt.callLater(popover.fitToWindow)
        }
    }

    contentItem: FocusScope {
        id: scope
        objectName: "outputPanel"
        focus: true
        implicitWidth: popover.width
        implicitHeight: panel.implicitHeight

        // 數字鍵直接選裝置；焦點在面板裡任何地方都可以。焦點在浮層裡時殼的 Ctrl+O 收不到，這裡自己關
        Keys.onPressed: event => {
            const row = event.key - Qt.Key_1
            if (event.key === Qt.Key_O && event.modifiers === Qt.ControlModifier) {
                popover.close()
                event.accepted = true
            } else if (event.modifiers === Qt.NoModifier && row >= 0 && row < 9) {
                Output.select(row)
                event.accepted = true
            }
        }

        Panel {
            id: panel
            anchors.fill: parent
            title: qsTr("輸出")
            index: qsTr("Ctrl O")

            Column {
                id: content
                width: parent.width
                spacing: Theme.space4

                // 裝置多時（每個輸出路徑都列一次）只顯示 6 列、其餘捲動，面板不超出視窗
                Flickable {
                    id: deviceList
                    objectName: "outputDeviceList"
                    // 視窗放得下的高度（fitToWindow 量）；還沒量時不限
                    property real maxHeight: Number.POSITIVE_INFINITY
                    width: parent.width
                    height: Math.min(devices.implicitHeight, 6 * Theme.space12, maxHeight)
                    contentHeight: devices.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    WheelScroll { flickable: deviceList; lineHeight: Theme.space12 }

                    // 清單的高度或內容在打開後才排好（裝置列載入、量完視窗高度）時，焦點列可能是在
                    // 還很矮的清單上捲過的：排好後再捲一次，焦點列完整露出
                    onHeightChanged: Qt.callLater(deviceList.revealFocused)
                    onContentHeightChanged: Qt.callLater(deviceList.revealFocused)
                    function revealFocused() {
                        for (let i = 0; i < devices.children.length; ++i) {
                            if (devices.children[i].activeFocus) {
                                deviceList.reveal(devices.children[i])
                                return
                            }
                        }
                    }

                    // 方向鍵移到看不到的列時捲到它
                    function reveal(row) {
                        if (row.y < contentY)
                            contentY = row.y
                        else if (row.y + row.height > contentY + height)
                            contentY = row.y + row.height - height
                    }

                    Column {
                        id: devices
                        width: deviceList.width

                        Repeater {
                            model: Output.devices
                            delegate: RadioRow {
                                required property var modelData
                                required property int index
                                objectName: "outputRow" + index
                                width: devices.width
                                // 面板上第一次出現的「ASIO」加 ®（商標規則第 15 條，T-104）
                                text: modelData.path === "ASIO" ? "ASIO®" : modelData.path
                                detail: modelData.device
                                note: modelData.note
                                noteTone: modelData.noteTone
                                checked: modelData.selected
                                // 打開時焦點在選取的那列（也因此捲到看得到它）
                                focus: modelData.selected
                                onSelected: Output.select(index)
                                // 等版面排好（剛打開時列的位置與清單高度還在變）再捲
                                onActiveFocusChanged: if (activeFocus) Qt.callLater(deviceList.reveal, this)
                            }
                        }
                        Text {
                            visible: Output.devices.length === 0
                            text: qsTr("沒有可用的輸出裝置")
                            font: Theme.body
                            color: Theme.inkMuted
                        }
                    }
                }

                // 清單裡有 ASIO 時放商標標示（開啟 ASIO 的畫面，商標規則第 1 條 f 款，T-104）
                AsioTrademark {
                    objectName: "outputAsioMark"
                    width: parent.width
                    visible: Output.devices.some(row => row.path === "ASIO")
                }

                // 進階輸出模組的區塊；每一塊各自載入，壞掉只影響那一塊
                Repeater {
                    model: UiRegistry.outputPanelSections
                    delegate: PageArea {
                        required property var modelData
                        width: parent ? parent.width : 0
                        height: item ? item.implicitHeight : Theme.barHeight
                        source: modelData.source
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Theme.hairline
                    color: Theme.line
                }

                // 訊號路徑：來源 → 處理 → 送到 DAC
                Row {
                    objectName: "signalPath"
                    visible: Output.active
                    spacing: Theme.space4

                    Readout {
                        label: qsTr("來源")
                        value: Output.sourceText
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        tone: "muted"
                    }
                    Readout {
                        label: qsTr("處理")
                        value: Output.processingText
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        tone: "muted"
                    }
                    Readout {
                        label: qsTr("送到 DAC")
                        value: Output.formatText
                    }
                }

                Item {
                    width: parent.width
                    height: Math.max(hint.implicitHeight, advanced.implicitHeight)

                    Text {
                        id: hint
                        anchors.left: parent.left
                        anchors.right: advanced.visible ? advanced.left : parent.right
                        anchors.rightMargin: Theme.space4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("切換會在目前曲目立即生效。按 1、2、3 可直接選裝置。")
                        font: Theme.small
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                    }
                    // 設定頁（另一張卡）登記後才出現
                    Button {
                        id: advanced
                        objectName: "outputAdvanced"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: UiRegistry.pageIds.indexOf("settings") >= 0
                        text: qsTr("輸出進階設定")
                        onClicked: {
                            popover.close()
                            Navigator.open("settings", {"section": "output"})
                        }
                    }
                }
            }
        }
    }
}
