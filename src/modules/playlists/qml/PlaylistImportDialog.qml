pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Pl

// 匯入結果（畫面規格 S-09，US-12；T-51）：PlaylistImport.result 有內容時打開。
// - 有找不到的：標題「匯入「<檔名>」」、讀數「曲目」「已找到」「找不到」、找不到的清單（等寬路徑＋原因，
//   最高 240 可捲動）、重新對應區（T-52：原本的資料夾、現在的資料夾＋「瀏覽」、「套用並重新檢查」）、
//   按鈕「略過」（找不到的項目已經保留在清單裡，關閉）；重新檢查後全部找到時變「完成」。
//   播放清單頁的「重新對應資料夾」開同一個對話框（標題「重新對應資料夾」）。
// - 讀不到、不支援（.fpl 結構不符，T-53）：一段文字＋「關閉」，沒有建立播放清單。
// 關閉（按鈕或 Esc）後換下一個檔（PlaylistImport.dismiss）。
Dialog {
    id: dialog

    // Dialog 本身有 final 的 result，所以不叫 result
    readonly property var importResult: PlaylistImport.result
    readonly property bool hasMissing: importResult.kind === "missing"
    readonly property var missingItems: importResult.missingItems ?? []
    // 重新檢查後全部找到：按鈕變「完成」，重新對應區收起來（T-52）
    readonly property bool allFound: hasMissing && Number(importResult.missing ?? 0) === 0

    // 數量用千分位（介面規範 §6.1）
    function count(value) {
        return Number(value ?? 0).toLocaleString(Qt.locale("en_US"), "f", 0)
    }

    objectName: "playlistImportDialog"
    // 播放清單頁開的（S-08「重新對應資料夾」）沒有檔名，標題用按鈕的字
    title: importResult.mode === "playlist" ? qsTr("重新對應資料夾")
                                            : qsTr("匯入「%1」").arg(importResult.fileName ?? "")
    message: importResult.message ?? ""
    cancelText: allFound ? qsTr("完成") : hasMissing ? qsTr("略過") : qsTr("關閉")
    confirmVisible: false
    onClosed: PlaylistImport.dismiss()

    Connections {
        target: PlaylistImport
        function onResultChanged() {
            const result = PlaylistImport.result
            if (result.kind === undefined)
                return
            // 預填：存過的對應規則的建議，沒有時找不到的路徑的共同資料夾；套用後是使用者輸入的
            oldRoot.text = result.oldRoot ?? ""
            newRoot.text = result.newRoot ?? ""
            dialog.open()
        }
        function onNewRootChosen(folder) {
            newRoot.text = folder
        }
    }

    contentItem: Item {
        implicitHeight: dialog.hasMissing ? missing.implicitHeight : text.implicitHeight

        Text {
            id: text
            objectName: "importMessage"
            visible: !dialog.hasMissing
            width: parent.width
            text: dialog.message
            font: Theme.body
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }

        Column {
            id: missing
            visible: dialog.hasMissing
            width: parent.width
            spacing: Theme.space4

            Flow {
                width: parent.width
                spacing: Theme.space6

                Readout {
                    objectName: "importTotal"
                    label: qsTr("曲目")
                    value: dialog.count(dialog.importResult.total)
                }
                Readout {
                    objectName: "importFound"
                    label: qsTr("已找到")
                    value: dialog.count(dialog.importResult.found)
                }
                Readout {
                    objectName: "importMissing"
                    label: qsTr("找不到")
                    value: dialog.count(dialog.importResult.missing)
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.hairline
                color: Theme.line
            }

            // 找不到的清單：最高 240（10 × space6），其餘捲動
            ListView {
                id: list
                objectName: "importMissingList"
                width: parent.width
                height: Math.min(contentHeight, 10 * Theme.space6)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: dialog.missingItems
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: list; lineHeight: Theme.rowHeightCompact }

                delegate: Item {
                    id: entry
                    required property var modelData
                    width: list.width
                    height: Theme.rowHeightCompact

                    Text {
                        anchors.left: parent.left
                        anchors.right: reason.left
                        anchors.rightMargin: Theme.space4
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.path
                        font: Theme.monoSmall
                        color: Theme.ink
                        elide: Text.ElideMiddle
                    }
                    Text {
                        id: reason
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space3
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.reason
                        font: Theme.small
                        color: Theme.inkMuted
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: Theme.hairline
                        color: Theme.line
                    }
                }
            }

            // 重新對應區（S-09，T-52）：「原本的資料夾 → 現在的資料夾」，套用後重新檢查並更新上面的數字與清單
            Column {
                id: remap
                objectName: "importRemap"
                visible: !dialog.allFound
                width: parent.width
                spacing: Theme.space3

                Text {
                    text: qsTr("重新對應資料夾")
                    font: Theme.label
                    color: Theme.ink
                }
                TextField {
                    id: oldRoot
                    objectName: "remapOldRoot"
                    width: parent.width
                    label: qsTr("原本的資料夾")
                }
                Item {
                    width: parent.width
                    implicitHeight: newRoot.implicitHeight

                    TextField {
                        id: newRoot
                        objectName: "remapNewRoot"
                        anchors.left: parent.left
                        anchors.right: browse.left
                        anchors.rightMargin: Theme.space3
                        label: qsTr("現在的資料夾")
                        onAccepted: apply.clicked()
                    }
                    Button {
                        id: browse
                        objectName: "remapBrowse"
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        text: qsTr("瀏覽")
                        onClicked: PlaylistImport.chooseNewRoot()
                    }
                }
                Button {
                    id: apply
                    objectName: "remapApply"
                    anchors.right: parent.right
                    variant: "solid"
                    text: qsTr("套用並重新檢查")
                    busy: PlaylistImport.remapping
                    enabled: oldRoot.text.trim().length > 0 && newRoot.text.trim().length > 0
                    onClicked: PlaylistImport.applyRemap(oldRoot.text.trim(), newRoot.text.trim())
                }
            }
        }
    }
}
