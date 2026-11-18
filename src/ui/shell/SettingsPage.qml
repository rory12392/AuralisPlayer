pragma ComponentBehavior: Bound
import QtQuick

// 設定（畫面規格 S-16）：內容區單欄、分區用 label 標題與髮絲線、設定即時生效（沒有「儲存」鈕）。
// 分區：音樂資料夾（核心）→ 輸出（核心：和輸出面板相同的裝置清單，選了立即生效並記住）→ 各模組登記的區塊（UiRegistry.settingsSections：DSD、ReplayGain、
// 歌詞…，每塊各自載入，壞掉只讓那一塊顯示 ErrorBlock）→ 外觀（主題，AC-19-2）→ 關於。
// 開頁參數：section＝分區名稱時捲到那一區；report＝open 時打開掃描報告（S-17）。
Item {
    id: page

    // 畫面規格 S-16：一欄最寬 720
    readonly property int columnMaxWidth: 720
    readonly property var themeModes: ["system", "void", "paper"]
    // 開頁參數要捲到的分區。輸出裝置清單在工作執行緒列舉、晚一點才排好，內容變高時再捲一次；
    // 使用者自己捲了就不再捲
    property string pendingSection

    function scrollToSection(name) {
        for (let i = 0; i < column.children.length; ++i) {
            const child = column.children[i]
            if (child.sectionId === name) {
                flick.contentY = Math.min(child.y, Math.max(0, flick.contentHeight - flick.height))
                return
            }
        }
    }

    Component.onCompleted: {
        // 輸出分區的裝置清單：每次開頁重新列舉（拔掉的裝置從清單消失）
        Output.refreshDevices()
        const params = Navigator.params
        if (params.section) {
            page.pendingSection = params.section
            Qt.callLater(page.scrollToSection, params.section)
        }
        // 等頁面放進視窗（對話框的浮層與尺寸才算得出來）再開
        if (params.report === "open")
            Qt.callLater(report.open)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.space8
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: flick }
        onContentHeightChanged: if (page.pendingSection.length > 0) page.scrollToSection(page.pendingSection)
        onMovementStarted: page.pendingSection = ""

        Column {
            id: column
            x: Theme.space8
            y: Theme.space8
            width: Math.min(flick.width - 2 * Theme.space8, page.columnMaxWidth)
            spacing: Theme.space8

            Text {
                text: qsTr("設定")
                font: Theme.heading
                color: Theme.ink
            }

            SettingsFolders {
                width: column.width
                onReportRequested: report.open()
            }

            // 輸出（US-02）：和輸出面板（S-14）相同的「輸出路徑＋裝置」清單，模式（WASAPI 獨占／共享）
            // 就是選哪一列。選了立即生效、播放中從目前位置繼續（AC-02-6），並記進 settings.ini
            SettingsSection {
                objectName: "settingsOutput"
                width: column.width
                title: qsTr("輸出")
                sectionId: "output"

                Text {
                    width: parent.width
                    text: qsTr("輸出裝置與模式。切換立即生效，重新啟動後沿用；記住的裝置不在時改用預設裝置。")
                    font: Theme.small
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                }
                // 選了之後清單整個重建：焦點留在這個範圍裡、交給新的選取列，方向鍵才能接著按
                FocusScope {
                    width: parent.width
                    height: outputRows.implicitHeight

                    Column {
                        id: outputRows
                        width: parent.width

                        Repeater {
                            model: Output.devices
                            delegate: RadioRow {
                                required property var modelData
                                required property int index
                                objectName: "settingsOutputRow" + index
                                width: outputRows.width
                                // 頁面上第一次出現的「ASIO」加 ®（商標規則第 15 條，T-104）
                                text: modelData.path === "ASIO" ? "ASIO®" : modelData.path
                                detail: modelData.device
                                note: modelData.note
                                noteTone: modelData.noteTone
                                checked: modelData.selected
                                focus: modelData.selected
                                onSelected: Output.select(index)
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
                // 清單裡有 ASIO 時：這裡也能選（設定）ASIO，放商標標示（商標規則第 1 條 f 款，T-104）
                AsioTrademark {
                    objectName: "settingsOutputAsioMark"
                    width: parent.width
                    visible: Output.devices.some(row => row.path === "ASIO")
                }
            }

            // 外掛（AC-03-0，T-104）：外掛清單、匯入、停用、移除，ASIO 外掛的下載說明與商標標示
            SettingsPlugins {
                width: column.width
            }

            // 模組的設定區塊（例：進階輸出的 DSD 與 ReplayGain、歌詞）；模組停用時沒有
            Repeater {
                model: UiRegistry.settingsSections
                delegate: PageArea {
                    id: moduleSection
                    required property var modelData
                    readonly property string sectionId: modelData.id
                    objectName: "settingsSection-" + modelData.id
                    width: column.width
                    height: item ? item.implicitHeight : Theme.barHeight
                    source: modelData.source
                }
            }

            SettingsSection {
                width: column.width
                title: qsTr("外觀")
                sectionId: "appearance"

                Item {
                    width: parent.width
                    height: Math.max(themeLabel.implicitHeight, themeControl.implicitHeight)
                    Column {
                        id: themeLabel
                        anchors.left: parent.left
                        anchors.right: themeControl.left
                        anchors.rightMargin: Theme.space4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space1
                        Text {
                            text: qsTr("主題")
                            font: Theme.body
                            color: Theme.ink
                        }
                        Text {
                            width: parent.width
                            text: qsTr("跟隨系統時，Windows 切換深淺色後介面會跟著變。")
                            font: Theme.small
                            color: Theme.inkMuted
                            wrapMode: Text.Wrap
                        }
                    }
                    SegmentedControl {
                        id: themeControl
                        objectName: "themeControl"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        label: qsTr("主題")
                        model: [{ text: qsTr("跟隨系統") }, { text: qsTr("深色") }, { text: qsTr("淺色") }]
                        currentIndex: Math.max(0, page.themeModes.indexOf(ThemeController.mode))
                        // 寫進 settings.ini 的 ui/theme，重新啟動後保留（AC-19-2）
                        onActivated: index => ThemeController.mode = page.themeModes[index]
                    }
                }
            }

            SettingsSection {
                width: column.width
                title: qsTr("關於")
                sectionId: "about"

                Flow {
                    width: parent.width
                    spacing: Theme.space8
                    Readout {
                        label: qsTr("版本")
                        value: Qt.application.version
                    }
                    Readout {
                        label: qsTr("授權")
                        value: "GPLv3"
                    }
                    Readout {
                        label: qsTr("原始碼")
                        value: "https://github.com/rory12392/AuralisPlayer"
                    }
                }
                // ASIO 外掛隨主程式安裝：「關於」放 ASIO Compatible 標誌與商標聲明
                // （Steinberg 商標規則第 1 條 e 款、第 15 條，T-105）
                AsioTrademark {
                    objectName: "aboutAsioTrademark"
                    width: parent.width
                }
            }
        }
    }

    ScanReportDialog {
        id: report
    }
}
