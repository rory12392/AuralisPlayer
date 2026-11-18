import QtQuick

// 輸出按鈕（畫面規格 S-02）：第一行「<輸出路徑>　<裝置名稱>」，第二行等寬「<取樣率> / <位元深度>」，
// 右側狀態標籤（位元完美 amber、原因 neutral、錯誤 signal）。窄視窗（compact）只留取樣率與狀態點，
// 完整內容在 ToolTip（介面規範 §4）。按下或 Ctrl+O 開關輸出面板；開著時 surfaceRaised 底、ink 框
// （共用 Button 的 checked 外觀）。資料都來自 Output。
Button {
    id: button

    property bool compact: false

    // Button 以 text 當可讀名稱；畫面上的內容由下面的 contentItem 自己排
    text: Output.accessibleText
    checked: Output.panelOpen
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    onClicked: Output.panelOpen = !Output.panelOpen

    contentItem: Item {
        implicitWidth: Theme.panelWidth
        implicitHeight: lines.implicitHeight

        Column {
            id: lines
            anchors.left: parent.left
            anchors.right: tag.left
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1

            Text {
                visible: !button.compact
                width: parent.width
                text: Output.deviceName.length > 0 ? qsTr("%1　%2").arg(Output.pathName).arg(Output.deviceName)
                                                   : Output.pathName
                font: Theme.body
                color: Theme.ink
                elide: Text.ElideRight
            }
            Text {
                objectName: "outputFormatText"
                visible: text.length > 0
                width: parent.width
                text: Output.formatText
                font: Theme.monoSmall
                color: Theme.inkMuted
                elide: Text.ElideRight
            }
        }

        StatusTag {
            id: tag
            objectName: "outputStatusTag"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: button.compact ? "" : Output.statusText
            variant: Output.statusVariant
        }
    }

    ToolTip {
        visible: button.compact && (button.hovered || button.visualFocus) && !Output.panelOpen
        text: Output.accessibleText
    }

    OutputPanel {
        id: panel
        x: button.width - width
        y: -height - Theme.space2
        onClosed: {
            Output.panelOpen = false
            // Esc 或點外面關閉後，焦點回到輸出按鈕（畫面規格 S-14）
            button.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    Connections {
        target: Output
        function onPanelOpenChanged() {
            if (Output.panelOpen)
                panel.open()
            else
                panel.close()
        }
    }
}
