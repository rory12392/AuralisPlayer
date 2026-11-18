pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs

// 設定頁的「外掛」分區（畫面規格 S-16，AC-03-0，T-104）：上方「匯入外掛…」，下面是外掛清單
// （每列：名稱、類型、版本、狀態、啟用開關、移除）。官方 ASIO 外掛那一列一定在（沒安裝時是「未安裝」），
// 列下方另外有 ASIO 的狀態句、說明、下載頁按鈕與商標標示。匯入、移除、停用都是重新啟動後生效。
// 資料都來自 Plugins；程式不下載任何東西，下載頁只交給瀏覽器開。
SettingsSection {
    id: section

    objectName: "settingsPlugins"
    title: qsTr("外掛")
    sectionId: "plugins"

    Item {
        width: section.width
        height: Math.max(intro.implicitHeight, importButton.implicitHeight)
        Text {
            id: intro
            anchors.left: parent.left
            anchors.right: importButton.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("匯入、停用或移除外掛，重新啟動後生效。程式不會從網路下載外掛。")
            font: Theme.small
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }
        Button {
            id: importButton
            objectName: "pluginsImport"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("匯入外掛…")
            onClicked: importDialog.open()
        }
    }

    // 上一個操作的結果：成功是「重新啟動後生效。」，失敗用 signal 寫原因
    Text {
        objectName: "pluginsMessage"
        width: section.width
        visible: Plugins.message.length > 0
        text: Plugins.message
        font: Theme.small
        color: Plugins.messageIsError ? Theme.signal : Theme.ink
        wrapMode: Text.Wrap
    }

    Repeater {
        model: Plugins.rows
        delegate: Column {
            id: pluginRow
            required property var modelData
            required property int index
            objectName: "pluginRow-" + modelData.id
            width: section.width
            spacing: Theme.space2

            Item {
                width: pluginRow.width
                height: Math.max(Theme.rowHeight, info.implicitHeight)

                Column {
                    id: info
                    anchors.left: parent.left
                    anchors.right: controls.left
                    anchors.rightMargin: Theme.space4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space1
                    Text {
                        width: parent.width
                        text: pluginRow.modelData.name
                        font: Theme.body
                        color: Theme.ink
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: pluginRow.modelData.version.length > 0
                              ? qsTr("%1　%2").arg(pluginRow.modelData.typeText)
                                              .arg(pluginRow.modelData.version)
                              : pluginRow.modelData.typeText
                        font: Theme.monoSmall
                        color: Theme.inkMuted
                    }
                }
                Row {
                    id: controls
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space4
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space1
                        StatusTag {
                            objectName: "pluginState-" + pluginRow.modelData.id
                            text: pluginRow.modelData.stateText
                            variant: pluginRow.modelData.tone
                        }
                        Text {
                            objectName: "pluginPending-" + pluginRow.modelData.id
                            visible: text.length > 0
                            text: pluginRow.modelData.pendingText
                            font: Theme.small
                            color: Theme.inkMuted
                        }
                    }
                    Checkbox {
                        objectName: "pluginEnabled-" + pluginRow.modelData.id
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pluginRow.modelData.canToggle
                        text: qsTr("啟用")
                        checked: pluginRow.modelData.enabled
                        Accessible.description: pluginRow.modelData.name
                        onToggled: Plugins.setEnabled(pluginRow.modelData.id, checked)
                    }
                    Button {
                        objectName: "pluginRemove-" + pluginRow.modelData.id
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pluginRow.modelData.canRemove
                        text: qsTr("移除")
                        Accessible.description: pluginRow.modelData.name
                        onClicked: Plugins.confirmRemove(pluginRow.modelData.id)
                    }
                    // 隨主程式安裝的外掛：只能停用，不能移除（T-105）
                    Text {
                        objectName: "pluginBuiltin-" + pluginRow.modelData.id
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pluginRow.modelData.builtin
                        text: qsTr("內建")
                        font: Theme.small
                        color: Theme.inkMuted
                    }
                }
            }

            // 官方 ASIO 外掛：狀態句、說明、下載頁與商標標示（第 1 條 f 款的標誌位置）
            Column {
                objectName: "pluginsAsio"
                visible: pluginRow.modelData.asio
                width: pluginRow.width
                spacing: Theme.space2
                Text {
                    objectName: "pluginsAsioStatus"
                    width: parent.width
                    text: qsTr("ASIO 外掛：%1").arg(pluginRow.modelData.asioStatus)
                    font: Theme.body
                    color: Theme.ink
                    wrapMode: Text.Wrap
                }
                Item {
                    width: parent.width
                    height: Math.max(asioHelp.implicitHeight, releaseButton.implicitHeight)
                    Text {
                        id: asioHelp
                        anchors.left: parent.left
                        anchors.right: releaseButton.left
                        anchors.rightMargin: Theme.space4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("ASIO 外掛隨主程式安裝，提供 ASIO 輸出與原生 DSD；不需要時可以停用。"
                                   + "要重新安裝時，從 Release 頁下載和主程式相同版本（%1）的外掛 zip，"
                                   + "解開後按「匯入外掛…」選裡面的 plugin.json。").arg(Qt.application.version)
                        font: Theme.small
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                    }
                    Button {
                        id: releaseButton
                        objectName: "pluginsRelease"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("開啟下載頁")
                        Accessible.description: Plugins.releaseUrl
                        onClicked: Plugins.openReleasePage()
                    }
                }
                AsioTrademark {
                    width: parent.width
                }
            }

            Rectangle {
                width: pluginRow.width
                height: Theme.hairline
                color: Theme.line
            }
        }
    }

    // 系統的開檔對話框：從「下載」開始（外掛 zip 通常解在那裡；不是音樂資料夾），選 plugin.json 或 DLL
    FileDialog {
        id: importDialog
        title: qsTr("匯入外掛")
        fileMode: FileDialog.OpenFile
        currentFolder: Plugins.dialogFolder
        nameFilters: [qsTr("AuralisPlayer 外掛 (plugin.json *.dll)")]
        onAccepted: Plugins.importFile(selectedFile)
    }
}
